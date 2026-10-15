#include "mediapreview.h"
#include <QAudioOutput>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QVideoWidget>
#include <QVideoFrame>

namespace {
QString clockText(qint64 ms) {
    const auto seconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QChar('0')).arg((seconds / 60) % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}
}
bool MediaPreview::accepts(const QString &path) {
    static const QSet<QString> extensions{"mp3","wav","flac","ogg","opus","aac","m4a","wma","mp4","m4v","mov","mkv","webm","avi","wmv","mpg","mpeg","ts","3gp"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
MediaPreview::MediaPreview(QWidget *parent) : QWidget(parent) {
    setObjectName("mediaPreview"); auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this); m_video = new QVideoWidget(this); m_video->setObjectName("previewVideo"); m_video->setMinimumSize(160, 120); m_video->setAspectRatioMode(Qt::KeepAspectRatio);
    auto *audioLabel = new QLabel(tr("♪\nÁudio"), this); audioLabel->setAlignment(Qt::AlignCenter); m_stack->addWidget(audioLabel); m_stack->addWidget(m_video); layout->addWidget(m_stack, 1);
    m_status = new QLabel(this); m_status->setObjectName("mediaStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
    m_seek = new QSlider(Qt::Horizontal, this); m_seek->setObjectName("mediaSeek"); m_seek->setAccessibleName(tr("Posição de reprodução")); m_seek->setRange(0, 10000); m_seek->setEnabled(false); layout->addWidget(m_seek);
    m_time = new QLabel("00:00:00 / 00:00:00", this); m_time->setObjectName("mediaTime"); layout->addWidget(m_time);
    auto *buttons = new QHBoxLayout; m_play = new QPushButton(tr("Reproduzir"), this); m_play->setObjectName("mediaPlay"); m_play->setEnabled(false);
    m_stop = new QPushButton(tr("Parar"), this); m_stop->setObjectName("mediaStop"); m_stop->setEnabled(false);
    m_mute = new QPushButton(tr("Silenciar"), this); m_mute->setObjectName("mediaMute"); m_mute->setCheckable(true);
    buttons->addWidget(m_play); buttons->addWidget(m_stop); buttons->addWidget(m_mute); buttons->addStretch(); layout->addLayout(buttons);
    auto *volume = new QHBoxLayout; volume->addWidget(new QLabel(tr("Volume"), this)); m_volume = new QSlider(Qt::Horizontal, this); m_volume->setObjectName("mediaVolume"); m_volume->setRange(0, 100); m_volume->setValue(50); m_volume->setAccessibleName(tr("Volume de reprodução")); volume->addWidget(m_volume); layout->addLayout(volume);
    m_audio = new QAudioOutput(this); m_audio->setObjectName("previewAudioOutput"); m_audio->setVolume(0.5f);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) { m_audio->setVolume(float(value) / 100); });
    connect(m_mute, &QPushButton::toggled, m_audio, &QAudioOutput::setMuted);
    connect(m_play, &QPushButton::clicked, this, [this] {
        if (!m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
        else { if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) m_player->setPosition(0); m_player->play(); }
    });
    connect(m_stop, &QPushButton::clicked, this, [this] { if (m_player) { m_player->stop(); m_player->setPosition(0); } });
    auto seek = [this] { if (m_player && m_player->isSeekable()) m_player->setPosition(qint64(double(m_seek->sliderPosition()) / 10000 * m_player->duration())); };
    connect(m_seek, &QSlider::sliderReleased, this, seek);
    connect(m_seek, &QSlider::actionTriggered, this, [this, seek] { if (!m_seek->isSliderDown()) seek(); });
}
MediaPreview::~MediaPreview() { release(); }
void MediaPreview::release() {
    // Destroy the old backend before releasing its device or accepting a new path.
    auto *old = m_player; m_player = nullptr;
    if (old) { old->disconnect(); old->stop(); old->setSourceDevice(nullptr); delete old; }
    delete m_file; m_file = nullptr; m_video->videoSink()->setVideoFrame({});
    m_play->setEnabled(false); m_play->setText(tr("Reproduzir")); m_stop->setEnabled(false); m_seek->setEnabled(false); m_seek->setValue(0); m_time->setText("00:00:00 / 00:00:00");
}
void MediaPreview::setPath(const QString &path) {
    release(); m_stack->setCurrentIndex(0); m_status->setText(tr("Carregando mídia…"));
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.isJunction() || !QFileInfo::exists(path)) { m_status->setText(tr("Arquivo indisponível ou link não suportado.")); emit loaded(false, m_status->text()); return; }
    m_file = new QFile(path, this);
    if (!m_file->open(QIODevice::ReadOnly)) { m_status->setText(tr("Não foi possível ler a mídia: %1").arg(m_file->errorString())); delete m_file; m_file = nullptr; emit loaded(false, m_status->text()); return; }
    auto *player = new QMediaPlayer(this); player->setObjectName("previewMediaPlayer"); m_player = player; player->setAudioOutput(m_audio); player->setVideoOutput(m_video);
    connect(player, &QMediaPlayer::mediaStatusChanged, player, [this, player](QMediaPlayer::MediaStatus status) {
        if (player != m_player) return;
        if (status == QMediaPlayer::LoadedMedia) { m_play->setEnabled(true); m_stop->setEnabled(true); m_seek->setEnabled(player->isSeekable()); m_status->setText(tr("Pronto. Clique em Reproduzir.")); emit loaded(true, m_status->text()); }
        else if (status == QMediaPlayer::EndOfMedia) m_status->setText(tr("Fim da reprodução."));
    });
    connect(player, &QMediaPlayer::errorOccurred, player, [this, player](QMediaPlayer::Error, const QString &message) { if (player != m_player) return; m_play->setEnabled(false); m_stop->setEnabled(false); m_seek->setEnabled(false); m_status->setText(tr("Não foi possível reproduzir. Formato/codec indisponível ou arquivo inválido. %1").arg(message)); emit loaded(false, m_status->text()); });
    connect(player, &QMediaPlayer::hasVideoChanged, player, [this, player](bool video) { if (player == m_player) m_stack->setCurrentWidget(video ? static_cast<QWidget *>(m_video) : m_stack->widget(0)); });
    connect(player, &QMediaPlayer::playbackStateChanged, player, [this, player](QMediaPlayer::PlaybackState state) { if (player == m_player) m_play->setText(state == QMediaPlayer::PlayingState ? tr("Pausar") : tr("Reproduzir")); });
    connect(player, &QMediaPlayer::positionChanged, player, [this, player] { if (player == m_player) updateTime(); }); connect(player, &QMediaPlayer::durationChanged, player, [this, player] { if (player == m_player) updateTime(); });
    connect(player, &QMediaPlayer::seekableChanged, player, [this, player](bool seekable) { if (player == m_player) m_seek->setEnabled(seekable); });
    player->setSourceDevice(m_file, QUrl::fromLocalFile(info.absoluteFilePath()));
}
void MediaPreview::updateTime() {
    if (!m_player) return;
    m_time->setText(clockText(m_player->position()) + " / " + clockText(m_player->duration()));
    if (!m_seek->isSliderDown()) m_seek->setValue(m_player->duration() > 0 ? int(double(m_player->position()) / m_player->duration() * 10000) : 0);
}
