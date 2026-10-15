#pragma once
#include <QWidget>
class QMediaPlayer; class QAudioOutput; class QVideoWidget; class QFile;
class QLabel; class QPushButton; class QSlider; class QStackedWidget;
class MediaPreview final : public QWidget {
    Q_OBJECT
public:
    explicit MediaPreview(QWidget *parent = nullptr);
    ~MediaPreview() override;
    static bool accepts(const QString &path);
    void setPath(const QString &path);
    void release();
signals:
    void loaded(bool success, QString message);
private:
    QMediaPlayer *m_player = nullptr;
    QFile *m_file = nullptr;
    QAudioOutput *m_audio;
    QVideoWidget *m_video;
    QStackedWidget *m_stack;
    QLabel *m_status, *m_time;
    QPushButton *m_play, *m_stop, *m_mute;
    QSlider *m_seek, *m_volume;
    void updateTime();
};
