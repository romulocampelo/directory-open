#include "ocr.h"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
namespace{
QString string(const winrt::hstring &value){return QString::fromWCharArray(value.c_str(),int(value.size()));}
}
OcrResult recognizePreview(const OcrRequest &request,const std::atomic_bool &cancel){
    OcrResult result;if(cancel){result.state=OcrState::Cancelled;result.message="Reconhecimento cancelado antes de iniciar.";return result;}
    if(!request.languagesOnly&&(request.image.isNull()||request.image.width()>4096||request.image.height()>4096||request.image.sizeInBytes()>64*1024*1024)){result.message="Use uma prévia raster de até 4096 pixels e 64 MiB.";return result;}
    try{
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        // This guard must be constructed before all WinRT objects, so every
        // object and operation releases before the worker uninitializes COM.
        const auto apartment=qScopeGuard([]{winrt::uninit_apartment();});
        using namespace winrt::Windows::Media::Ocr;using namespace winrt::Windows::Graphics::Imaging;
        // Cached C++/WinRT static factories can outlive the last COM apartment
        // between short workers. Own fresh factory references inside this task;
        // do not use constructors/static methods that enter the process cache.
        const auto ocr=winrt::try_get_activation_factory<OcrEngine,IOcrEngineStatics>();
        if(!ocr){result.state=OcrState::Unavailable;result.message="OCR do Windows indisponível nesta sessão.";return result;}
        for(const auto &language:ocr.AvailableRecognizerLanguages()){if(result.languages.size()>=128)break;result.languages.append(string(language.LanguageTag()));}
        if(result.languages.isEmpty()){result.state=OcrState::Unavailable;result.message="O Windows não informou idiomas OCR instalados. Nenhum pacote foi instalado.";return result;}
        if(request.languagesOnly){result.state=OcrState::Completed;return result;}
        OcrEngine engine{nullptr};if(request.language.isEmpty())engine=ocr.TryCreateFromUserProfileLanguages();else if(result.languages.contains(request.language)){
            using namespace winrt::Windows::Globalization;const auto factory=winrt::try_get_activation_factory<Language,ILanguageFactory>();if(factory)engine=ocr.TryCreateFromLanguage(factory.CreateLanguage(request.language.toStdWString()));
        }
        if(!engine){result.state=OcrState::Unavailable;result.message="O idioma solicitado não está disponível no OCR do Windows.";return result;}result.language=string(engine.RecognizerLanguage().LanguageTag());
        const int limit=qMin(3200,int(ocr.MaxImageDimension()));if(limit<1){result.state=OcrState::Unavailable;result.message="O Windows não confirmou os limites do OCR.";return result;}
        auto image=request.image;if(image.width()>limit||image.height()>limit){image=image.scaled(limit,limit,Qt::KeepAspectRatio,Qt::SmoothTransformation);result.reduced=true;}image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);result.recognizedSize=image.size();
        if(cancel){result.state=OcrState::Cancelled;result.message="Reconhecimento cancelado.";return result;}
        const auto bitmapFactory=winrt::try_get_activation_factory<SoftwareBitmap,ISoftwareBitmapFactory>();
        using namespace winrt::Windows::Security::Cryptography;
        const auto crypto=winrt::try_get_activation_factory<CryptographicBuffer,ICryptographicBufferStatics>();
        if(!bitmapFactory||!crypto){result.state=OcrState::Unavailable;result.message="Conversão de imagem WinRT indisponível.";return result;}
        auto bitmap=bitmapFactory.CreateWithAlpha(BitmapPixelFormat::Bgra8,image.width(),image.height(),BitmapAlphaMode::Premultiplied);
        const auto buffer=crypto.CreateFromByteArray(winrt::array_view<const uint8_t>(image.constBits(),image.constBits()+image.sizeInBytes()));bitmap.CopyFromBuffer(buffer);
        auto operation=engine.RecognizeAsync(bitmap);QElapsedTimer timer;timer.start();bool stopped=false,timedOut=false;
        while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){if(!stopped&&(cancel||timer.elapsed()>30000)){timedOut=!cancel;operation.Cancel();stopped=true;}QThread::msleep(10);}
        if(stopped){result.state=timedOut?OcrState::Failed:OcrState::Cancelled;result.message=timedOut?"O OCR excedeu 30 segundos e foi cancelado.":"Reconhecimento cancelado.";return result;}
        const auto recognized=operation.GetResults();result.text=string(recognized.Text());if(result.text.size()>512*1024){result.text.truncate(512*1024);result.state=OcrState::Limited;result.message="Texto parcial: limite de 512 mil caracteres.";}else{result.state=OcrState::Completed;result.message=result.text.isEmpty()?"Nenhum texto foi reconhecido na prévia.":"Texto reconhecido da prévia exibida. Confira com a imagem.";}return result;
    }catch(const winrt::hresult_error &error){result.state=cancel?OcrState::Cancelled:OcrState::Failed;result.message="OCR do Windows: "+string(error.message());return result;}
}
