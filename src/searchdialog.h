#pragma once
#include <QDialog>
#include "filesearch.h"
class QLineEdit;
class QCheckBox;
class QTreeWidget;
class QLabel;
class QPushButton;
class QComboBox;
class SearchDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SearchDialog(const QString &root, QWidget *parent = nullptr);
    ~SearchDialog() override;
signals:
    void openFolderRequested(QString path);
    void openFileRequested(QString path);
protected:
    void reject() override;
private:
    QLineEdit *m_root, *m_name, *m_instance;
    QCheckBox *m_wildcard, *m_matchPath;
    QComboBox *m_engine;
    QTreeWidget *m_results;
    QLabel *m_status, *m_filterSummary;
    QPushButton *m_search, *m_cancel, *m_browse, *m_open, *m_openFile, *m_filterButton;
    FolderRules m_filters;
    SearchJob *m_job = nullptr;
    bool m_closeWhenFinished = false;
    bool m_cancelRequested = false;
    void startSearch();
    void showSummary(const SearchSummary &summary, bool final);
    void openFolder();
    void openEntry();
};
