#include "guardedtransfer.h"
#include "batchrename.h"
#include <windows.h>
namespace {
bool stable(const RenameIdentity &identity){return (identity.indexHigh||identity.indexLow)&&identity.created&&!(identity.attributes&FILE_ATTRIBUTE_REPARSE_POINT);}
bool same(const RenameIdentity &before,const RenameIdentity &now){
    if(!stable(now)||before.volume!=now.volume||before.indexHigh!=now.indexHigh||before.indexLow!=now.indexLow||before.created!=now.created)return false;
    if(before.attributes&FILE_ATTRIBUTE_DIRECTORY)return (now.attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    return before==now;
}
}
TransferApproval approveTransfer(const OperationRequest &request){
    TransferApproval approval;approval.request=request;
    if((request.kind!=OperationKind::Copy&&request.kind!=OperationKind::Move)||request.sources.isEmpty()||request.sources.size()>256){approval.error="A fila aceita cópia/movimentação de até 256 origens por tarefa.";return approval;}
    approval.error=validateOperationRequest(request);if(!approval.error.isEmpty())return approval;
    qsizetype units=request.destination.size();for(const auto &source:request.sources){if(source.size()>1024*1024-units){approval.error="Os caminhos da tarefa excedem 2 MiB UTF-16.";return approval;}units+=source.size();const auto current=inspectRenameItem(source);if(current.state!=RenameProbeState::Present||!stable(current.identity)){approval.error="Origem sem identidade estável: "+source+"\n"+current.error;return approval;}approval.sources.append(current.identity);approval.sourceNames.append(current.actualName);}
    const auto destination=inspectRenameItem(request.destination);if(destination.state!=RenameProbeState::Present||!stable(destination.identity)||!(destination.identity.attributes&FILE_ATTRIBUTE_DIRECTORY)){approval.error="Destino sem identidade estável de pasta. "+destination.error;return approval;}approval.destination=destination.identity;approval.retainedBytes=units*2+approval.sources.size()*qsizetype(sizeof(RenameIdentity));for(const auto &name:approval.sourceNames)approval.retainedBytes+=name.size()*2;return approval;
}
QString revalidateTransfer(const TransferApproval &approval){
    if(!approval.error.isEmpty())return approval.error;
    if(approval.request.sources.isEmpty()||approval.request.sources.size()>256||approval.sources.size()!=approval.request.sources.size()||approval.sourceNames.size()!=approval.sources.size()||(approval.request.kind!=OperationKind::Copy&&approval.request.kind!=OperationKind::Move))return "A aprovação da transferência é inconsistente.";
    const auto error=validateOperationRequest(approval.request);if(!error.isEmpty())return error;
    for(int i=0;i<approval.sources.size();++i){const auto current=inspectRenameItem(approval.request.sources[i]);if(current.state!=RenameProbeState::Present||(!same(approval.sources[i],current.identity)||current.actualName!=approval.sourceNames[i]))return "A origem mudou desde a inclusão na fila. Remova a tarefa e prepare uma nova: "+approval.request.sources[i];}
    const auto current=inspectRenameItem(approval.request.destination);if(current.state!=RenameProbeState::Present||!same(approval.destination,current.identity))return "O destino foi substituído desde a inclusão na fila. Prepare uma nova tarefa.";return {};
}
OperationResult performApprovedTransfer(const TransferApproval &approval,const std::atomic_bool &cancel,const OperationProgress &progress){
    if(cancel){OperationResult result;result.state=OperationState::Cancelled;return result;}
    const auto error=revalidateTransfer(approval);if(!error.isEmpty()){OperationResult result;result.error=error;for(const auto &source:approval.request.sources)result.items.append({source,{},OperationItemState::NotStarted,{},{}});return result;}
    return performOperation(approval.request,cancel,progress);
}
