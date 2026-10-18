#pragma once
#include <QImage>
#include <QThread>
#include <atomic>
enum class OcrState{Completed,Unavailable,Failed,Cancelled,Limited};
struct OcrRequest{QImage image;QString language;bool languagesOnly=false;};
struct OcrResult{OcrState state=OcrState::Failed;QString text,message,language;QStringList languages;QSize recognizedSize;bool reduced=false;};
OcrResult recognizePreview(const OcrRequest &request,const std::atomic_bool &cancel);
class OcrJob final:public QThread{
    Q_OBJECT
public:
    explicit OcrJob(OcrRequest request,QObject *parent=nullptr):QThread(parent),m_request(std::move(request)){}
    ~OcrJob()override{cancel();wait();}
    void cancel(){m_cancel=true;}
    OcrResult takeResult(){return std::move(m_result);} // after wait
protected:
    void run()override{m_result=recognizePreview(m_request,m_cancel);}
private:
    OcrRequest m_request;OcrResult m_result;std::atomic_bool m_cancel{false};
};
