#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
class QFileSystemModel;
class QListView;
class QSortFilterProxyModel;
class ThumbnailCache;

// Owns visible-item demand. Painting only consults cached model facts/results.
class ThumbnailView final : public QObject {
    Q_OBJECT
public:
    ThumbnailView(QListView *view, QFileSystemModel *model, QSortFilterProxyModel *proxy, ThumbnailCache *cache);
    void setSize(int pixels);
    void requestVisible();
protected: bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QListView *m_view;
    QFileSystemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QPointer<ThumbnailCache> m_cache;
    QTimer m_timer;
    void updateDemand();
};
