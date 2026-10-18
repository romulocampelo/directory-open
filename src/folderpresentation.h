#pragma once
#include <QDate>
#include <QFileInfo>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QRegularExpression>
#include <optional>

struct FolderRules {
    QString patterns;
    qint64 minimumBytes = -1, maximumBytes = -1;
    QDate modifiedFrom, modifiedTo;
    bool files = true, folders = true, hidden = true;
    bool operator==(const FolderRules &) const = default;
};
QString validateFolderRules(const FolderRules &rules);
std::optional<FolderRules> decodeFolderRules(const QJsonValue &value);
QJsonObject encodeFolderRules(const FolderRules &rules);
bool matchesFolderRules(const QFileInfo &file, const FolderRules &rules);
struct FolderRuleFacts {
    QString name;
    bool directory = false, hidden = false;
    qint64 bytes = 0;
    QDate modified;
};
class FolderRuleMatcher {
public:
    void setRules(const FolderRules &rules);
    bool matches(const QFileInfo &file) const;
    bool matches(const FolderRuleFacts &file) const;
private:
    FolderRules m_rules;
    QVector<QRegularExpression> m_patterns;
};
struct FileGroup { QString key, label; };
FileGroup fileGroup(const QFileInfo &file, int mode);
constexpr int GroupHeaderRole = Qt::UserRole + 510;

// Owns filtering, group order and header identity. All rows remain real files.
class FolderFilter final : public QSortFilterProxyModel {
public:
    explicit FolderFilter(QObject *parent = nullptr);
    void update(const QString &root, const QString &text, const FolderRules &rules);
    void setGrouping(int mode);
    int grouping() const { return m_grouping; }
    QVariant data(const QModelIndex &index, int role) const override;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;
private:
    QString m_root, m_text;
    FolderRuleMatcher m_matcher;
    int m_grouping = 0;
};
class GroupedFileDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    static int headerHeight(const QFontMetrics &metrics) { return metrics.height() + 8; }
};
class QWidget;
std::optional<FolderRules> editFolderRules(const FolderRules &rules, QWidget *parent, const QString &title = {});
