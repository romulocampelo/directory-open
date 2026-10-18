#pragma once
#include "renameidentity.h"
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <optional>
struct FileAnnotation{
    QString path,label,status,description,color;
    QStringList tags;
    int rating=0;
    QJsonObject extra;
};
struct AnnotationCatalog{QMap<QString,FileAnnotation> entries;QJsonObject extra;};
enum class AnnotationState{Missing,Loaded,Invalid,Unsupported};
struct AnnotationLoad{AnnotationState state=AnnotationState::Missing;AnnotationCatalog catalog;QString error;QByteArray digest;};
QString annotationIdentity(const RenameIdentity &identity);
QString validateAnnotation(const FileAnnotation &value);
QJsonObject encodeAnnotation(const FileAnnotation &value);
std::optional<FileAnnotation> decodeAnnotation(const QJsonValue &value);
AnnotationLoad loadAnnotations(const QString &path);
struct AnnotationRecord{QString path,key,error;FileAnnotation before,value;};
// Merge under lock, compare the approved note and stable file identity, then
// publish the catalog atomically. Does not modify user file bytes or streams.
QString saveAnnotations(const QString &path,const QVector<AnnotationRecord> &changes,const std::atomic_bool &cancel,AnnotationCatalog *volatileCatalog=nullptr);
struct AnnotationResult{QVector<AnnotationRecord> records;AnnotationCatalog catalog;QString error;bool cancelled=false,saved=false;};
class AnnotationJob final:public QThread{
    Q_OBJECT
public:
    AnnotationJob(QString catalogPath,QStringList files,AnnotationCatalog memory,QObject *parent);
    AnnotationJob(QString catalogPath,QVector<AnnotationRecord> changes,AnnotationCatalog memory,QObject *parent);
    ~AnnotationJob()override;
    void cancel(){m_cancel=true;}
    AnnotationResult takeResult(); // after wait
protected:
    void run()override;
private:
    QString m_path;QStringList m_files;QVector<AnnotationRecord>m_changes;AnnotationCatalog m_memory;bool m_saving=false;
    std::atomic_bool m_cancel{false};AnnotationResult m_result;
};
