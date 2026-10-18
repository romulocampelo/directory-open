#pragma once
// Opt-in measurement build only. No instrumentation in the delivered runtime.
#ifdef DIROPEN_PRESENTATION_PROBE
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <array>
namespace presentationProbe {
enum Stage { Query, Format, Widgets, Layout, Paint, Count };
struct Sample { qint64 ns=0; int calls=0; };
inline thread_local std::array<Sample,Count> samples{};
struct Scope {
    Stage stage; QElapsedTimer timer;
    explicit Scope(Stage s):stage(s){timer.start();}
    ~Scope(){samples[stage].ns+=timer.nsecsElapsed();++samples[stage].calls;}
};
class Application : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject *receiver,QEvent *event) override {
        if(event->type()==QEvent::LayoutRequest){Scope scope(Layout);return QApplication::notify(receiver,event);}
        if(event->type()==QEvent::Paint){Scope scope(Paint);return QApplication::notify(receiver,event);}
        return QApplication::notify(receiver,event);
    }
};
}
#endif
