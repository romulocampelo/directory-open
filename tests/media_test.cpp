#include "mediapreview.h"
#include "previewwidget.h"
#include "mainwindow.h"
#include "filepanel.h"
#include "mediafixture.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QDockWidget>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoWidget>
#include <windows.h>
#include <psapi.h>

class MediaTest final : public QObject {
    Q_OBJECT
    void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),qint64(bytes.size())); }
    QMediaPlayer *player(PreviewWidget &widget) { return widget.findChild<QMediaPlayer *>("previewMediaPlayer"); }
private slots:
    void audioLoadsWithoutAutoplayAndSupportsPlayPauseStopVolumeAndSeek() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());const auto path=fixture.filePath("música.wav");write(path,MediaFixture::wave());PreviewWidget widget;widget.resize(600,500);widget.show();widget.setPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(widget.result().kind,PreviewKind::Media,15000);auto *media=player(widget);QVERIFY(media);QCOMPARE(media->playbackState(),QMediaPlayer::StoppedState);QVERIFY(media->duration()>=3900);
        widget.findChild<QSlider *>("mediaVolume")->setValue(27);QCOMPARE(widget.findChild<QAudioOutput *>()->volume(),0.27f);widget.findChild<QPushButton *>("mediaMute")->click();QVERIFY(widget.findChild<QAudioOutput *>()->isMuted());
        auto *play=widget.findChild<QPushButton *>("mediaPlay");play->click();QTRY_COMPARE_WITH_TIMEOUT(media->playbackState(),QMediaPlayer::PlayingState,5000);QTRY_VERIFY_WITH_TIMEOUT(media->position()>0,5000);play->click();QTRY_COMPARE(media->playbackState(),QMediaPlayer::PausedState);
        auto *seek=widget.findChild<QSlider *>("mediaSeek");QVERIFY(seek->isEnabled());seek->setValue(5000);QVERIFY(QMetaObject::invokeMethod(seek,"sliderReleased"));QTRY_VERIFY(media->position()>=1900);
        widget.findChild<QPushButton *>("mediaStop")->click();QCOMPARE(media->playbackState(),QMediaPlayer::StoppedState);QCOMPARE(media->position(),qint64(0));
        const auto capture=qEnvironmentVariable("DIROPEN_MEDIA_CAPTURE");if(!capture.isEmpty()){QVERIFY(QTest::qWaitForWindowExposed(&widget));QVERIFY(widget.grab().save(capture));}
        widget.setPath({});QVERIFY(!player(widget));QVERIFY(QFile::rename(path,fixture.filePath("released.wav")));
    }
    void originalVideoDecodesRealFramesAndCanBeReleased() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());const auto path=fixture.filePath("vídeo.avi");write(path,MediaFixture::video());PreviewWidget widget;widget.resize(640,500);widget.show();widget.setPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(widget.result().kind,PreviewKind::Media,15000);auto *media=player(widget);QVERIFY(media);auto *video=widget.findChild<QVideoWidget *>();QVERIFY(video);int frames=0;QSize frameSize;QColor decodedColor;
        connect(video->videoSink(),&QVideoSink::videoFrameChanged,&widget,[&](const QVideoFrame &frame){if(frame.isValid()){++frames;frameSize=frame.size();if(frames==1){const auto image=frame.toImage();if(!image.isNull())decodedColor=image.pixelColor(image.width()/2,image.height()/2);}}});
        widget.findChild<QPushButton *>("mediaPlay")->click();QTRY_VERIFY_WITH_TIMEOUT(frames>0,10000);QCOMPARE(frameSize,QSize(160,120));QVERIFY(media->hasVideo());
        QVERIFY(decodedColor.isValid());QVERIFY(decodedColor.blue()>decodedColor.red()+50);
        widget.releaseMedia();QVERIFY(!player(widget));QVERIFY(QFile::rename(path,fixture.filePath("released.avi")));
    }
    void corruptAndMissingMediaAreErrorsAndLateSelectionCannotOverrideText() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());write(fixture.filePath("bad.mp4"),"invalid data");write(fixture.filePath("a.wav"),MediaFixture::wave());write(fixture.filePath("plain.txt"),"texto preservado");PreviewWidget widget;widget.setPath(fixture.filePath("bad.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(widget.result().kind,PreviewKind::Error,15000);QVERIFY(widget.result().message.contains("codec"));
        widget.setPath(fixture.filePath("a.wav"));widget.setPath(fixture.filePath("plain.txt"));QTRY_COMPARE(widget.result().kind,PreviewKind::Text);QCOMPARE(widget.result().text,QString("texto preservado"));QVERIFY(!player(widget));QTest::qWait(200);QCOMPARE(widget.result().kind,PreviewKind::Text);
        widget.setPath(fixture.filePath("missing.wav"));QCOMPARE(widget.result().kind,PreviewKind::Error);
    }
    void reloadAndRepeatedSelectionsOwnOnlyOnePlayerAndReleaseOldDevices() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());const auto a=fixture.filePath("a.wav"),b=fixture.filePath("b.wav");write(a,MediaFixture::wave());write(b,MediaFixture::wave());PreviewWidget widget;
        for(int i=0;i<4;++i){widget.setPath(i%2?a:b);QTRY_COMPARE_WITH_TIMEOUT(widget.result().kind,PreviewKind::Media,15000);QCOMPARE(widget.findChildren<QMediaPlayer *>().size(),1);widget.findChild<QPushButton *>("mediaMute")->setChecked(true);widget.findChild<QPushButton *>("mediaPlay")->click();}
        widget.reload();QTRY_COMPARE(widget.result().kind,PreviewKind::Media);QCOMPARE(player(widget)->playbackState(),QMediaPlayer::StoppedState);widget.setPath({});QVERIFY(widget.findChildren<QMediaPlayer *>().isEmpty());QVERIFY(QFile::remove(a));QVERIFY(QFile::remove(b));
    }
    void enlargedViewerReusesPausedPlayerAndClosingDockReleasesIt() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());const auto path=fixture.filePath("a.avi");write(path,MediaFixture::video());MainWindow window(fixture.path(),fixture.path(),false);window.show();auto *view=window.leftPanel()->filesView();
        QTRY_VERIFY(view->model()->rowCount(view->rootIndex())>0);const auto index=view->model()->index(0,0,view->rootIndex());view->setCurrentIndex(index);view->selectionModel()->select(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        auto *dock=window.findChild<QDockWidget *>("previewDock");dock->show();auto *preview=window.findChild<PreviewWidget *>();QTRY_COMPARE_WITH_TIMEOUT(preview->result().kind,PreviewKind::Media,15000);QPointer<QMediaPlayer> original=player(*preview);QVERIFY(original);preview->findChild<QPushButton *>("mediaMute")->setChecked(true);preview->findChild<QPushButton *>("mediaPlay")->click();QTRY_VERIFY(original->position()>0);original->pause();
        QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog *>("viewerDialog");QVERIFY(dialog);QCOMPARE(player(*preview),original.data());QCOMPARE(original->playbackState(),QMediaPlayer::PausedState);
            QTimer::singleShot(8000,dialog,&QDialog::reject);
            if(QGuiApplication::platformName()=="windows") { QVERIFY(QTest::qWaitForWindowExposed(dialog));dialog->activateWindow();QVERIFY(QTest::qWaitForWindowActive(dialog));QTest::keyClick(QApplication::focusWidget()?QApplication::focusWidget():dialog,Qt::Key_F11);QTRY_VERIFY(dialog->isFullScreen());QTest::keyClick(QApplication::focusWidget()?QApplication::focusWidget():dialog,Qt::Key_F11);QTRY_VERIFY(!dialog->isFullScreen()); }
            dialog->reject();});window.findChild<QAction *>("viewFile")->trigger();
        QCOMPARE(player(*preview),original.data());dock->hide();QTRY_VERIFY(original.isNull());QVERIFY(QFile::rename(path,fixture.filePath("released.avi")));
    }
    void completedLoadAndReleaseMeasureFirstAndRepeatedAudioTasks() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());const auto path=fixture.filePath("a.wav");write(path,MediaFixture::wave());
        for(int pass=0;pass<2;++pass){FILETIME c{},e{},kb{},ub{},ka{},ua{};GetProcessTimes(GetCurrentProcess(),&c,&e,&kb,&ub);QElapsedTimer timer;timer.start();
            {PreviewWidget widget;widget.setPath(path);QTRY_COMPARE_WITH_TIMEOUT(widget.result().kind,PreviewKind::Media,15000);widget.releaseMedia();}
            const auto ms=timer.elapsed();GetProcessTimes(GetCurrentProcess(),&c,&e,&ka,&ua);auto ticks=[](const FILETIME &t){return(quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime;};PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
            qInfo()<<"Audio task 4s WAV load + release pass"<<pass<<"elapsed ms"<<ms<<"CPU ms"<<double(ticks(ka)+ticks(ua)-ticks(kb)-ticks(ub))/10000<<"cumulative process peak MiB"<<double(memory.PeakWorkingSetSize)/(1024*1024);
        }
    }
};
QTEST_MAIN(MediaTest)
#include "media_test.moc"
