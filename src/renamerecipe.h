#pragma once
#include <QString>
#include <QVector>
#include <QJsonValue>
#include <optional>
enum class RenameRuleKind { Prefix, Suffix, Replace, Slice, Lower, Upper, Trim };
struct RenameRule { RenameRuleKind kind=RenameRuleKind::Prefix;QString text,replacement;int start=1,count=0;bool caseSensitive=true;bool operator==(const RenameRule&)const=default; };
struct RenameRecipe { QString name="Minha receita";QVector<RenameRule> rules;bool operator==(const RenameRecipe&)const=default; };
QString validateRenameRules(const QVector<RenameRule>&rules);
struct RenameText { QString text,error; };
RenameText applyRenameRules(QString text,const QVector<RenameRule>&rules);
QJsonObject encodeRenameRecipe(const RenameRecipe&recipe);
std::optional<RenameRecipe> decodeRenameRecipe(const QJsonValue&value);
struct RenameRecipeFile { RenameRecipe recipe;QString error; };
RenameRecipeFile readRenameRecipe(const QString&path);
QString saveRenameRecipe(const QString&path,const RenameRecipe&recipe);
