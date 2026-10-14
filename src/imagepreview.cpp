#include "imagepreview.h"
#include <QBuffer>
#include <QImageReader>

DecodedImage decodeImagePreview(const QByteArray &bytes, int edge) {
    DecodedImage result;
    if (bytes.size() > maxImageFileBytes || edge < 1 || edge > 4096) {
        result.error = "Imagem acima do limite de leitura ou resolução inválida."; return result;
    }
    QBuffer input; input.setData(bytes); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 128 * 1024 * 1024) {
        result.error = "Dimensões da imagem inválidas ou acima de 128 megapixels."; return result;
    }
    const bool transpose = int(reader.transformation()) >= 4;
    result.originalSize = transpose ? size.transposed() : size;
    if (size.width() > edge || size.height() > edge) reader.setScaledSize(size.scaled(edge, edge, Qt::KeepAspectRatio));
    result.image = reader.read();
    if (result.image.isNull()) { result.error = "Não foi possível decodificar a imagem: " + reader.errorString(); return result; }
    // Some plugins ignore scaledSize. Normalize once before handing off a bitmap.
    if (result.image.width() > edge || result.image.height() > edge)
        result.image = result.image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}
