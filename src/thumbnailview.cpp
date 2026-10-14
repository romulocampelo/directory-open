#include "thumbnailview.h"
#include "thumbnailcache.h"
#include <QApplication>
#include <QEvent>
#include <QFileSystemModel>
#include <QHelpEvent>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QToolTip>

namespace {
class ThumbnailDelegate final : public QStyledItemDelegate {
public:
    ThumbnailDelegate(QListView *view, QFileSystemModel *model, QSortFilterProxyModel *proxy, ThumbnailCache *cache)
        : QStyledItemDelegate(view), m_view(view), m_model(model), m_proxy(proxy), m_cache(cache) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem background(option); initStyleOption(&background, index);
        background.showDecorationSelected = true;
        const auto title = background.text; background.text.clear(); background.icon = {};
        background.features &= ~QStyleOptionViewItem::HasDecoration;
        m_view->style()->drawControl(QStyle::CE_ItemViewItem, &background, painter, m_view);
        // We replace the native text/icon layout. Some Windows styles suppress
        // selection for an empty native content rectangle, so own the full tile.
        if (option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(option.rect.adjusted(2, 2, -2, -2), background.palette.brush(QPalette::Highlight));
        const auto sourceIndex = m_proxy->mapToSource(index);
        const auto source = thumbnailSource(m_model->fileInfo(sourceIndex));
        const auto *entry = source && m_cache ? m_cache->find(*source) : nullptr;
        QRect area = option.rect.adjusted(12, 8, -12, -30);
        painter->save(); painter->setClipRect(option.rect); painter->setRenderHint(QPainter::SmoothPixmapTransform);
        if (entry && !entry->image.isNull()) {
            const auto size = entry->image.size().scaled(area.size(), Qt::KeepAspectRatio);
            const auto rect = QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size);
            painter->drawImage(rect, entry->image);
        } else {
            const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            icon.paint(painter, area, Qt::AlignCenter, option.state.testFlag(QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
            if (entry && !entry->error.isEmpty()) {
                painter->setPen(option.palette.color(QPalette::Text));
                painter->drawText(area.adjusted(0, area.height() - 22, 0, 0), Qt::AlignCenter, QStringLiteral("!"));
            }
        }
        const auto textColor = option.state.testFlag(QStyle::State_Selected) ? background.palette.color(QPalette::HighlightedText)
            : index.data(Qt::ForegroundRole).value<QColor>().isValid() ? index.data(Qt::ForegroundRole).value<QColor>() : option.palette.color(QPalette::Text);
        painter->setPen(textColor); painter->setFont(option.font);
        const auto textArea = option.rect.adjusted(6, option.rect.height() - 27, -6, -5);
        painter->drawText(textArea, Qt::AlignCenter, option.fontMetrics.elidedText(title, Qt::ElideMiddle, textArea.width()));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return m_view->gridSize(); }
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &, const QModelIndex &index) override {
        if (!index.isValid() || event->type() != QEvent::ToolTip) return false;
        QString text = m_model->filePath(m_proxy->mapToSource(index));
        const auto source = thumbnailSource(m_model->fileInfo(m_proxy->mapToSource(index)));
        const auto *entry = source && m_cache ? m_cache->find(*source) : nullptr;
        if (entry && !entry->error.isEmpty()) text += '\n' + entry->error;
        QToolTip::showText(event->globalPos(), text, view); return true;
    }
private:
    QListView *m_view;
    QFileSystemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QPointer<ThumbnailCache> m_cache;
};
}
ThumbnailView::ThumbnailView(QListView *view, QFileSystemModel *model, QSortFilterProxyModel *proxy, ThumbnailCache *cache)
    : QObject(view), m_view(view), m_model(model), m_proxy(proxy), m_cache(cache) {
    m_timer.setSingleShot(true); m_timer.setInterval(35);
    connect(&m_timer, &QTimer::timeout, this, &ThumbnailView::updateDemand);
    m_view->setItemDelegate(new ThumbnailDelegate(view, model, proxy, cache));
    m_view->installEventFilter(this); m_view->viewport()->installEventFilter(this);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &ThumbnailView::requestVisible);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &ThumbnailView::requestVisible);
    connect(proxy, &QAbstractItemModel::layoutChanged, this, &ThumbnailView::requestVisible);
    connect(proxy, &QAbstractItemModel::modelReset, this, &ThumbnailView::requestVisible);
    connect(proxy, &QAbstractItemModel::rowsInserted, this, &ThumbnailView::requestVisible);
    connect(proxy, &QAbstractItemModel::rowsRemoved, this, &ThumbnailView::requestVisible);
    connect(proxy, &QAbstractItemModel::dataChanged, this, &ThumbnailView::requestVisible);
    connect(cache, &ThumbnailCache::changed, m_view->viewport(), qOverload<>(&QWidget::update));
    cache->setDemand(this, {}); setSize(144);
}
void ThumbnailView::setSize(int pixels) {
    m_view->setGridSize(QSize(pixels + 28, pixels + 40));
    m_view->setIconSize(QSize(pixels, pixels)); requestVisible();
}
void ThumbnailView::requestVisible() { if (!m_timer.isActive()) m_timer.start(); }
bool ThumbnailView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_view && event->type() == QEvent::Hide && m_cache) m_cache->setDemand(this, {});
    if (event->type() == QEvent::Paint || event->type() == QEvent::Resize || event->type() == QEvent::Show) requestVisible();
    return QObject::eventFilter(watched, event);
}
void ThumbnailView::updateDemand() {
    if (!m_cache) return;
    QVector<ThumbnailSource> demand;
    if (m_view->isVisible()) {
        QSet<int> seen; const auto grid = m_view->gridSize();
        // Sample the visible grid, not the whole directory. Half-cell strides
        // include clipped edge cells and avoid O(directory size) scans.
        for (int y = 0; y < m_view->viewport()->height() && demand.size() < 128; y += qMax(1, grid.height() / 2)) {
            for (int x = 0; x < m_view->viewport()->width() && demand.size() < 128; x += qMax(1, grid.width() / 2)) {
                const auto index = m_view->indexAt(QPoint(x, y));
                if (!index.isValid() || seen.contains(index.row())) continue;
                seen.insert(index.row());
                const auto source = thumbnailSource(m_model->fileInfo(m_proxy->mapToSource(index)));
                if (source) demand.append(*source);
            }
        }
    }
    m_cache->setDemand(this, demand);
}
