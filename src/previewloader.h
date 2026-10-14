#pragma once
#include <QImage>
#include <QString>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <optional>
#include <atomic>

enum class PreviewKind { Empty, Image, Text, Pdf, Error };
enum class PreviewEncoding { Auto, Utf8, Utf16LE, Utf16BE, Windows1252, Latin1 };
struct PreviewRequest { QString path; int page = 0; PreviewEncoding encoding = PreviewEncoding::Auto; quint64 serial = 0; };
struct PreviewResult {
    PreviewKind kind = PreviewKind::Empty;
    QString path, text, encoding, message;
    QImage image;
    QSize originalSize;
    int page = 0, pages = 0;
    quint64 serial = 0;
    bool truncated = false, textCandidate = false;
};

// One persistent worker and one replaceable pending request. Native decoding
// is never performed on the GUI thread. Results retain their request identity.
class PreviewWorker final : public QThread {
    Q_OBJECT
public:
    explicit PreviewWorker(QObject *parent = nullptr);
    ~PreviewWorker() override;
    quint64 submit(PreviewRequest request);
    void stop();
    std::optional<PreviewResult> takeResult();
signals:
    void ready();
protected:
    void run() override;
private:
    QMutex m_mutex;
    QWaitCondition m_wake;
    std::optional<PreviewRequest> m_pending;
    std::optional<PreviewResult> m_result;
    bool m_notified = false;
    std::atomic<quint64> m_serial{0};
    bool m_stopping = false;
};
