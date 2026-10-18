#include "synchronization.h"
#include "batchrename.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <windows.h>
namespace {
bool directory(const RenameProbe &probe){return probe.state==RenameProbeState::Present&&(probe.identity.attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(probe.identity.attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&(probe.identity.indexHigh||probe.identity.indexLow)&&probe.identity.created;}
bool sameDirectory(const RenameIdentity &before,const RenameProbe &now){return directory(now)&&before.volume==now.identity.volume&&before.indexHigh==now.identity.indexHigh&&before.indexLow==now.identity.indexLow&&before.created==now.identity.created;}
bool relative(const QString &value){if(value.isEmpty()||QDir::isAbsolutePath(value)||value.contains('\\')||value.contains(QChar(0))||value.size()>32768)return false;for(const auto &part:value.split('/'))if(!validateItemName(part).isEmpty())return false;return true;}
QString targetError(const SynchronizationItem &item){const auto current=inspectRenameItem(item.target);if(!item.targetIdentity)return current.state==RenameProbeState::Missing?QString{}:"O destino deixou de estar livre. Gere outra prévia: "+item.target;if(current.state!=RenameProbeState::Present||current.identity!=*item.targetIdentity||current.actualName!=item.targetName)return "O item de destino mudou desde a prévia: "+item.target;return {};}
QString parentError(const QString &root,const QString &path){
    auto cursor=QFileInfo(path).absolutePath();const auto clean=QDir::cleanPath(root);for(int depth=0;depth<128;++depth){const auto probe=inspectRenameItem(cursor);if(!directory(probe))return "Pasta intermediária indisponível ou link/junção: "+cursor;if(cursor.compare(clean,Qt::CaseInsensitive)==0)return {};const auto next=QFileInfo(cursor).absolutePath();if(next==cursor)break;cursor=next;}return "O caminho não pertence à raiz ou excede 128 níveis.";
}
}
bool SynchronizationPlan::valid()const{if(cancelled||!error.isEmpty()||items.size()>5000||changes()==0||changes()>256)return false;for(const auto &item:items)if(item.transfer&&!item.transfer->error.isEmpty())return false;return true;}
int SynchronizationPlan::changes()const{int count=0;for(const auto &item:items)if(item.transfer)++count;return count;}
SynchronizationPlan planSynchronization(const SynchronizationRequest &request,const std::atomic_bool &cancel){
    SynchronizationPlan plan;plan.request=request;if(request.entries.isEmpty()||request.entries.size()>5000){plan.error="Escolha resultados de uma comparação completa (até 5.000 linhas).";return plan;}
    qsizetype requestBytes=(request.left.size()+request.right.size())*2;for(const auto &entry:request.entries){requestBytes+=(entry.relativePath.size()+entry.leftPath.size()+entry.rightPath.size()+entry.leftDescription.size()+entry.rightDescription.size()+entry.detail.size())*2;if(requestBytes>4*1024*1024){plan.request.entries.clear();plan.error="A seleção da comparação excede o orçamento de 4 MiB. Escolha menos linhas.";return plan;}}
    const auto left=inspectRenameItem(request.left),right=inspectRenameItem(request.right);if(!directory(left)||!directory(right)){plan.error="As duas raízes precisam ser pastas normais e disponíveis.";return plan;}plan.leftIdentity=left.identity;plan.rightIdentity=right.identity;
    auto entries=request.entries;std::sort(entries.begin(),entries.end(),[](const ComparisonEntry&a,const ComparisonEntry&b){const auto da=a.relativePath.count('/'),db=b.relativePath.count('/');return da==db?a.relativePath.toCaseFolded()<b.relativePath.toCaseFolded():da<db;});
    const auto sourceRoot=QDir::cleanPath(request.toRight?request.left:request.right),targetRoot=QDir::cleanPath(request.toRight?request.right:request.left);QSet<QString>seen;QStringList covered;qsizetype retained=0;
    for(const auto &entry:entries){
        if(cancel){plan.cancelled=true;return plan;}if(!relative(entry.relativePath)||seen.contains(entry.relativePath.toCaseFolded())){plan.error="Caminho relativo inválido ou repetido no plano.";return plan;}seen.insert(entry.relativePath.toCaseFolded());retained+=entry.relativePath.size()*2;if(retained>4*1024*1024){plan.error="O plano excede o orçamento de 4 MiB de caminhos/identidades.";return plan;}
        SynchronizationItem item;item.relativePath=entry.relativePath;item.target=QDir(targetRoot).filePath(entry.relativePath);bool shadowed=false;for(const auto &prefix:covered)if(entry.relativePath.startsWith(prefix+'/',Qt::CaseInsensitive)){shadowed=true;break;}if(shadowed){item.message="Incluído na cópia da pasta superior; não será copiado duas vezes.";plan.items.append(std::move(item));continue;}
        const bool missing=request.toRight?entry.kind==ComparisonKind::LeftOnly:entry.kind==ComparisonKind::RightOnly;const bool conflict=entry.kind==ComparisonKind::MetadataDifferent||entry.kind==ComparisonKind::ContentDifferent;
        if(!missing&&!(conflict&&request.includeConflicts)){item.message=conflict?"Conflito preservado; não incluído.":"Sem cópia nesta direção; item preservado.";plan.items.append(std::move(item));continue;}
        const auto source=request.toRight?entry.leftPath:entry.rightPath;if(source.isEmpty()||QDir::cleanPath(QDir::fromNativeSeparators(source)).compare(QDir(sourceRoot).filePath(entry.relativePath),Qt::CaseInsensitive)!=0){plan.error="O caminho observado da origem não corresponde à raiz e ao item. Compare novamente.";return plan;}OperationRequest transfer;transfer.kind=OperationKind::Copy;transfer.sources={source};transfer.destination=QFileInfo(item.target).absolutePath();transfer.engine=TransferEngine::Windows;transfer.conflict=request.conflict;transfer.nativeUi=request.nativeUi;transfer.ownerWindow=request.ownerWindow;
        item.transfer=approveTransfer(transfer);auto &approval=*item.transfer;
        if(approval.error.isEmpty()){item.target=QDir(transfer.destination).filePath(approval.sourceNames.first());if(conflict&&(approval.sources.first().attributes&FILE_ATTRIBUTE_DIRECTORY))approval.error="Conflitos entre arquivo e pasta exigem revisão manual fora da sincronização.";}
        if(approval.error.isEmpty())approval.error=parentError(sourceRoot,source);if(approval.error.isEmpty())approval.error=parentError(targetRoot,item.target);
        if(approval.error.isEmpty()){
            const auto target=inspectRenameItem(item.target);
            if(missing&&target.state!=RenameProbeState::Missing)approval.error="O destino não está mais ausente. Compare novamente.";
            else if(!missing){if(target.state!=RenameProbeState::Present||target.identity.attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))approval.error="O conflito não é entre arquivos normais disponíveis.";else{item.targetIdentity=target.identity;item.targetName=target.actualName;}}
            if(missing&&!approval.sources.isEmpty()&&(approval.sources.first().attributes&FILE_ATTRIBUTE_DIRECTORY)&&approval.error.isEmpty())covered.append(entry.relativePath);
        }
        item.message=approval.error.isEmpty()?(missing?(approval.sources.first().attributes&FILE_ATTRIBUTE_DIRECTORY?"Copiar pasta faltante; conteúdo avaliado na execução.":QString("Copiar arquivo faltante — %1 bytes.").arg(approval.sources.first().bytes)):request.conflict==OperationConflict::KeepBoth?"Conflito: manter as duas versões.":"Conflito: perguntar no Windows antes de substituir."):approval.error;
        retained+=approval.retainedBytes+item.target.size()*2;if(retained>4*1024*1024){plan.error="O plano excede o orçamento de 4 MiB de caminhos/identidades.";return plan;}plan.items.append(std::move(item));if(plan.changes()>256){plan.error="Selecione até 256 origens por plano; uma pasta faltante inclui seus descendentes.";return plan;}
    }
    return plan;
}
OperationResult applySynchronization(const SynchronizationPlan &plan,const std::atomic_bool &cancel,const OperationProgress &progress){
    OperationResult result;if(cancel){result.state=OperationState::Cancelled;return result;}if(!plan.valid()){result.error="O plano de sincronização é inválido ou precisa de outra prévia.";return result;}
    const auto roots=[&]{return sameDirectory(plan.leftIdentity,inspectRenameItem(plan.request.left))&&sameDirectory(plan.rightIdentity,inspectRenameItem(plan.request.right));};
    auto check=[&](const SynchronizationItem &item){if(!item.transfer||item.transfer->request.kind!=OperationKind::Copy||item.transfer->request.engine!=TransferEngine::Windows)return QString("Ação de sincronização não suportada.");if(!roots())return QString("Uma raiz de sincronização foi substituída. Gere outra prévia.");auto error=revalidateTransfer(*item.transfer);if(error.isEmpty())error=targetError(item);if(error.isEmpty())error=parentError(plan.request.toRight?plan.request.left:plan.request.right,item.transfer->request.sources.first());if(error.isEmpty())error=parentError(plan.request.toRight?plan.request.right:plan.request.left,item.target);return error;};
    for(const auto &item:plan.items)if(item.transfer){if(cancel){result.state=OperationState::Cancelled;return result;}result.error=check(item);if(!result.error.isEmpty())return result;}
    quint32 done=0;for(const auto &item:plan.items)if(item.transfer){if(cancel){result.state=OperationState::Cancelled;return result;}const auto error=check(item);if(!error.isEmpty()){result.error=error;result.state=done?OperationState::Incomplete:OperationState::Failed;return result;}if(progress)progress(plan.changes(),done,item.relativePath);const auto copied=performApprovedTransfer(*item.transfer,cancel);result.items+=copied.items;if(copied.state!=OperationState::Completed){result.state=copied.state==OperationState::Cancelled?OperationState::Cancelled:done?OperationState::Incomplete:copied.state;result.error=copied.error;return result;}++done;}
    result.state=OperationState::Completed;if(progress)progress(plan.changes(),done,{});return result;
}
