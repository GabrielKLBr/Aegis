#include "Analysis.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
namespace av::microvm::analysis {
namespace {
bool FixedText(const char* text){for(size_t i=0;i<128;++i){const auto c=static_cast<unsigned char>(text[i]);if(!c){for(++i;i<128;++i)if(text[i])return false;return true;}if(c<32||c>126)return false;}return false;}
bool Valid(const Event& e,uint64_t boot){return e.magic==Magic&&e.boot==boot&&e.version==Version&&e.bytes==sizeof(e)&&e.sequence&&e.operationId&&e.operationId<=e.sequence&&
    Bit(e.domain)&&uint32_t(e.operation)>=1&&uint32_t(e.operation)<=16&&uint32_t(e.stage)>=1&&uint32_t(e.stage)<=2&&uint32_t(e.decision)<=2&&uint32_t(e.reason)<=6&&
    !(e.flags&~AllowedFlags)&&!e.reserved[0]&&!e.reserved[1]&&e.monotonicNs<(1ull<<62)&&e.beforeBytes<=512&&e.afterBytes<=512&&FixedText(e.resource)&&FixedText(e.secondary)&&
    bool(e.flags&Mutation)==Mutating(e.domain,e.operation);}
std::string DecisionName(Decision d){return d==Decision::Allow?"allow":d==Decision::Deny?"deny":"simulate";}
std::string ReasonName(Reason r){switch(r){case Reason::Observed:return "observed";case Reason::Policy:return "policy";case Reason::UnsupportedSimulation:return "unsupported-simulation";case Reason::Capacity:return "telemetry-capacity";case Reason::AccessDenied:return "access-denied";case Reason::OperationFailed:return "operation-failed";default:return "invalid-input";}}
std::string OperationName(Operation op){const char* names[]={"","create","open","read","write","rename","delete","load","unload","start","stop","send","receive","protect","exit","flush","query"};return names[uint32_t(op)];}
std::string Lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c+32);return s;}
std::string Escape(const std::string& value){std::string out;for(char c:value){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=c;}}return out;}
struct Finding {std::string rule,classification,reason;uint32_t pid=0;std::vector<uint64_t> evidence;};
std::vector<Finding> Findings(const std::vector<Record>& rows,bool complete){
    std::vector<Finding> out;std::map<uint32_t,std::map<std::string,std::pair<uint64_t,uint64_t>>> written;
    struct Pair {std::string path;uint64_t write,rename,ns;};
    std::map<uint32_t,std::vector<Pair>> renamed;std::map<std::pair<uint32_t,uint64_t>,uint64_t> services;std::set<uint32_t> bulkReported;
    for(const auto& row:rows){const auto& e=row.event;if(e.stage!=Stage::Result)continue;
        const bool applied=!e.status&&e.decision!=Decision::Deny;const auto path=Lower(e.resource);
        if(e.decision==Decision::Deny&&(e.reason==Reason::Policy||e.reason==Reason::UnsupportedSimulation))out.push_back({"policy-block-v1","suspicious","Operation was prevented by the configured policy; this does not confirm malware",e.pid,{e.sequence}});
        if(e.domain==Domain::Memory&&e.operation==Operation::Protect&&e.object==0x40&&!applied)
            out.push_back({"writable-executable-attempt-v1","suspicious","Writable/executable memory was requested and refused; no successful protection change is inferred",e.pid,{e.sequence}});
        if(!applied)continue;
        if(e.domain==Domain::Registry&&e.operation==Operation::Write&&
           (path.ends_with("\\run")||path.ends_with("\\runonce")))
            out.push_back({"persistence-registry-v1","suspicious","A startup configuration value was written; automatic execution is not inferred",e.pid,{e.sequence}});
        if(e.domain==Domain::Service&&e.operation==Operation::Create)services[{e.pid,e.related}]=e.sequence;
        if(e.domain==Domain::Service&&e.operation==Operation::Start&&services.count({e.pid,e.object}))
            out.push_back({"owned-service-start-v1","confirmed","An owned guest service was created and its real child process started; confirmation refers to this behavior",e.pid,{services[{e.pid,e.object}],e.sequence}});
        if(e.domain==Domain::Memory&&e.operation==Operation::Protect&&e.object==0x40)
            out.push_back({"writable-executable-memory-v1","suspicious","A guest memory region became writable and executable",e.pid,{e.sequence}});
        if(e.domain==Domain::File&&e.operation==Operation::Write&&(e.flags&ExistedBefore)&&e.beforeBytes&&e.oldHash!=e.newHash)
            written[e.pid][path]={e.sequence,row.observedNs};
        if(e.domain==Domain::File&&e.operation==Operation::Rename&&Lower(e.secondary).ends_with(".locked")){
            auto& previous=written[e.pid];const auto it=previous.find(path);
            if(it!=previous.end()&&row.observedNs>=it->second.second&&row.observedNs-it->second.second<=10000000000ull){
                auto& pairs=renamed[e.pid];pairs.erase(std::remove_if(pairs.begin(),pairs.end(),[&](const Pair& p){return row.observedNs-p.ns>10000000000ull;}),pairs.end());
                if(std::none_of(pairs.begin(),pairs.end(),[&](const Pair& p){return p.path==path;}))pairs.push_back({path,it->second.first,e.sequence,it->second.second});previous.erase(it);
                if(pairs.size()>=3&&bulkReported.insert(e.pid).second){std::vector<uint64_t> evidence;for(const auto& p:pairs){evidence.push_back(p.write);evidence.push_back(p.rename);}out.push_back({"bulk-rewrite-rename-v1","confirmed","At least three distinct synthetic files were overwritten and renamed to .locked by the same process within ten seconds; this confirms the pattern, not a malware identity",e.pid,evidence});}
            }
        }
    }
    if(!complete)out.push_back({"coverage-v1","inconclusive","The session failed, is unfinished, or telemetry was lost/rejected; absence of other findings is not a safe verdict",0,{}});
    if(out.empty())out.push_back({"observed-subset-v1","legitimate","No configured behavior pattern matched the complete observed subset; general file safety is not established",0,{}});
    return out;
}
}
std::string Quote(const std::string& text){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32||c>126){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
std::string DomainName(Domain d){const char* names[]={"","process","thread","module","file","registry","service","driver","network","memory","control"};return Bit(d)?names[uint32_t(d)]:"invalid";}
Domain ParseDomain(const std::string& s){for(uint32_t n=1;n<=10;++n)if(DomainName(Domain(n))==s)return Domain(n);throw std::runtime_error("Unknown analysis domain");}
void Trace::Configure(uint64_t boot,const Policy& policy){state::Require(boot&&ValidPolicy(policy),"Telemetry policy rejected");*this=Trace{};boot_=boot;policy_=policy;}
bool Trace::Accept(Event& e,uint64_t ns){
    if(finished_||!Valid(e,boot_)||e.sequence!=lastSequence_+1||ns<lastNs_||
       (e.stage==Stage::Intent&&(e.status||e.operationId!=e.sequence||e.decision!=Decision::Allow||e.reason!=Reason::Observed))){Reject();return false;}
    if(e.stage==Stage::Result){
        const auto p=std::find_if(pending_.begin(),pending_.end(),[&](const Event& b){return b.operationId==e.operationId;});
        if(p==pending_.end()){if(!dropped_){Reject();return false;}}
        else {
            if(e.pid!=p->pid||e.tid!=p->tid||e.user!=p->user||e.actorPid!=p->actorPid||e.actorTid!=p->actorTid||e.domain!=p->domain||e.operation!=p->operation||e.decision!=p->decision||e.reason!=p->reason||
               e.object!=p->object||e.oldHash!=p->oldHash||e.beforeBytes!=p->beforeBytes||std::memcmp(e.resource,p->resource,128)||std::memcmp(e.secondary,p->secondary,128)||
               ((e.flags^p->flags)&~ExistsAfter)|| (e.decision==Decision::Deny&&!e.status)){Reject();return false;}
            pending_.erase(p);
        }
        if(e.decision==Decision::Allow&&e.status)e.reason=(e.flags&InvalidText)?Reason::InvalidInput:e.status==0xC0000022u?Reason::AccessDenied:Reason::OperationFailed;
    }
    lastSequence_=e.sequence;lastNs_=ns;
    if(rows_.size()>=policy_.eventLimit||pending_.size()>=64){++dropped_;if(e.stage==Stage::Intent&&(e.flags&Mutation)){e.decision=Decision::Deny;e.reason=Reason::Capacity;}return true;}
    if(e.stage==Stage::Intent){
        if(e.flags&Mutation){const auto bit=Bit(e.domain);if(policy_.denyMask&bit){e.decision=Decision::Deny;e.reason=Reason::Policy;}
            else if(policy_.simulateMask&bit){e.decision=Simulatable(e)?Decision::Simulate:Decision::Deny;e.reason=Simulatable(e)?Reason::Policy:Reason::UnsupportedSimulation;}}
        pending_.push_back(e);
    }
    rows_.push_back({e,ns});return true;
}
void Trace::Save(state::Writer& w) const {w.Pod(uint32_t(Version));w.Pod(boot_);w.Pod(policy_);w.Pod(lastSequence_);w.Pod(lastNs_);w.Pod(dropped_);w.Pod(rejected_);w.Pod(uint32_t(finished_));w.Pod(uint32_t(complete_));w.Pod(uint32_t(rows_.size()));for(const auto& row:rows_)w.Pod(row);w.Pod(uint32_t(pending_.size()));for(const auto& row:pending_)w.Pod(row);}
void Trace::Load(state::Reader& r,uint64_t boot,const Policy& policy){
    Trace copy;copy.Configure(boot,policy);state::Require(r.Pod<uint32_t>()==Version&&r.Pod<uint64_t>()==boot,"Telemetry snapshot identity rejected");const auto stored=r.Pod<Policy>();state::Require(std::memcmp(&stored,&policy,sizeof(policy))==0,"Telemetry snapshot policy mismatch");
    copy.lastSequence_=r.Pod<uint64_t>();copy.lastNs_=r.Pod<uint64_t>();copy.dropped_=r.Pod<uint64_t>();copy.rejected_=r.Pod<uint64_t>();const auto f=r.Pod<uint32_t>(),c=r.Pod<uint32_t>();state::Require(f<=1&&c<=1,"Telemetry flags rejected");copy.finished_=f!=0;copy.complete_=c!=0;
    const auto count=r.Pod<uint32_t>();state::Require(count<=policy.eventLimit,"Telemetry snapshot capacity rejected");uint64_t sequence=0,ns=0;
    for(uint32_t i=0;i<count;++i){auto row=r.Pod<Record>();state::Require(Valid(row.event,boot)&&row.event.sequence>sequence&&row.event.sequence<=copy.lastSequence_&&row.observedNs>=ns&&row.observedNs<=copy.lastNs_,"Telemetry snapshot record rejected");sequence=row.event.sequence;ns=row.observedNs;copy.rows_.push_back(row);}
    const auto pending=r.Pod<uint32_t>();state::Require(pending<=64,"Telemetry pending quota rejected");std::set<uint64_t> ids;for(uint32_t i=0;i<pending;++i){auto row=r.Pod<Event>();state::Require(Valid(row,boot)&&row.stage==Stage::Intent&&row.sequence<=copy.lastSequence_&&ids.insert(row.operationId).second,"Telemetry pending record rejected");copy.pending_.push_back(row);}
    state::Require(!copy.complete_||(copy.finished_&&!copy.dropped_&&!copy.rejected_&&copy.pending_.empty()),"Telemetry completion rejected");
    if(!copy.dropped_&&!copy.rejected_){
        Trace replay;replay.Configure(boot,policy);
        for(const auto& row:copy.rows_){auto event=row.event;
            if(event.stage==Stage::Intent){event.decision=Decision::Allow;event.reason=Reason::Observed;}
            else {const auto before=std::find_if(replay.pending_.begin(),replay.pending_.end(),[&](const Event& e){return e.operationId==event.operationId;});state::Require(before!=replay.pending_.end(),"Telemetry snapshot result has no intent");event.reason=before->reason;}
            state::Require(replay.Accept(event,row.observedNs)&&std::memcmp(&event,&row.event,sizeof(event))==0,"Telemetry snapshot causality/decision rejected");
        }
        state::Require(replay.lastSequence_==copy.lastSequence_&&replay.lastNs_==copy.lastNs_&&replay.pending_.size()==copy.pending_.size(),"Telemetry snapshot counters rejected");
        for(size_t i=0;i<copy.pending_.size();++i)state::Require(std::memcmp(&replay.pending_[i],&copy.pending_[i],sizeof(Event))==0,"Telemetry snapshot pending identity rejected");
    }
    *this=std::move(copy);
}
std::string Trace::Json() const {
    std::ostringstream s;s<<"{\"schema\":1,\"rulesVersion\":1,\"boot\":"<<boot_<<",\"provenance\":\"validated-guest-input\",\"scope\":\"instrumented-guest-operations\",\"stateDifferenceScope\":\"file-and-registry-content-and-path\",\"hashAlgorithm\":\"FNV-1a-64\",\"complete\":"<<(Coherent()?"true":"false")<<",\"finished\":"<<(finished_?"true":"false")<<",\"dropped\":"<<dropped_<<",\"rejected\":"<<rejected_<<",\"pending\":"<<pending_.size()<<",\"policy\":{\"version\":1,\"denyMask\":"<<policy_.denyMask<<",\"simulateMask\":"<<policy_.simulateMask<<",\"eventLimit\":"<<policy_.eventLimit<<"},\"events\":[";
    bool first=true;for(const auto& row:rows_){const auto& e=row.event;if(!first)s<<',';first=false;s<<"{\"sequence\":"<<e.sequence<<",\"operationId\":"<<e.operationId<<",\"observedNs\":"<<row.observedNs<<",\"guestNs\":"<<e.monotonicNs<<",\"pid\":"<<e.pid<<",\"tid\":"<<e.tid<<",\"user\":"<<e.user<<",\"actorPid\":"<<e.actorPid<<",\"actorTid\":"<<e.actorTid<<",\"domain\":"<<Quote(DomainName(e.domain))<<",\"operation\":"<<Quote(OperationName(e.operation))<<",\"stage\":"<<Quote(e.stage==Stage::Intent?"intent":"result")<<",\"resource\":"<<Quote(e.resource)<<",\"secondary\":"<<Quote(e.secondary)<<",\"object\":"<<e.object<<",\"related\":"<<e.related<<",\"status\":"<<e.status<<",\"decision\":"<<Quote(DecisionName(e.decision))<<",\"reason\":"<<Quote(ReasonName(e.reason))<<",\"flags\":"<<e.flags<<",\"beforeBytes\":"<<e.beforeBytes<<",\"afterBytes\":"<<e.afterBytes<<",\"oldHash\":"<<e.oldHash<<",\"newHash\":"<<e.newHash<<'}';}
    s<<"],\"findings\":[";first=true;for(const auto& f:Findings(rows_,Coherent())){if(!first)s<<',';first=false;s<<"{\"rule\":"<<Quote(f.rule)<<",\"version\":1,\"classification\":"<<Quote(f.classification)<<",\"pid\":"<<f.pid<<",\"reason\":"<<Quote(f.reason)<<",\"evidence\":[";for(size_t i=0;i<f.evidence.size();++i){if(i)s<<',';s<<f.evidence[i];}s<<"]}";}
    s<<"],\"stateDifferences\":[";first=true;for(const auto& row:rows_){const auto& e=row.event;if(e.stage!=Stage::Result||e.status||e.decision==Decision::Deny||!(e.flags&Mutation)||(e.domain!=Domain::File&&e.domain!=Domain::Registry))continue;if(!first)s<<',';first=false;s<<"{\"sequence\":"<<e.sequence<<",\"domain\":"<<Quote(DomainName(e.domain))<<",\"operation\":"<<Quote(OperationName(e.operation))<<",\"resource\":"<<Quote(e.resource)<<",\"secondary\":"<<Quote(e.secondary)<<",\"existedBefore\":"<<((e.flags&ExistedBefore)?"true":"false")<<",\"existsAfter\":"<<((e.flags&ExistsAfter)?"true":"false")<<",\"beforeBytes\":"<<e.beforeBytes<<",\"afterBytes\":"<<e.afterBytes<<",\"oldHash\":"<<e.oldHash<<",\"newHash\":"<<e.newHash<<'}';}s<<"]}\n";return s.str();
}
std::string Trace::Html() const {
    std::ostringstream s;s<<"<!doctype html><html lang=\"pt-BR\"><meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'\"><title>GuardianAV — análise</title><style>body{font:16px system-ui;max-width:1200px;margin:2rem auto;padding:0 1rem}td,th{padding:.4rem;border-bottom:1px solid #ccc;text-align:left}table{border-collapse:collapse}code{overflow-wrap:anywhere}</style><h1>Análise do convidado</h1><p>Cobertura: "<<(Coherent()?"completa para o subconjunto observado":"inconclusiva")<<". Perdidos: "<<dropped_<<"; rejeitados: "<<rejected_<<". Regras v1. Dados recebidos da VM são entradas não confiáveis.</p><h2>Achados</h2><ul>";
    for(const auto& f:Findings(rows_,Coherent()))s<<"<li><b>"<<Escape(f.classification)<<"</b> — "<<Escape(f.rule)<<": "<<Escape(f.reason)<<" (processo "<<f.pid<<")</li>";
    s<<"</ul><h2>Linha do tempo</h2><table><tr><th>Seq.</th><th>Processo/thread observado</th><th>Autor: processo/thread</th><th>Operação</th><th>Recurso</th><th>Decisão</th><th>Resultado</th></tr>";
    for(const auto& r:rows_){const auto& e=r.event;s<<"<tr><td>"<<e.sequence<<"</td><td>"<<e.pid<<'/'<<e.tid<<"</td><td>"<<e.actorPid<<'/'<<e.actorTid<<"</td><td>"<<Escape(DomainName(e.domain)+" / "+OperationName(e.operation)+(e.stage==Stage::Intent?" / intent":" / result"))<<"</td><td><code>"<<Escape(e.resource)<<"</code> <code>"<<Escape(e.secondary)<<"</code></td><td>"<<Escape(DecisionName(e.decision))<<"</td><td>"<<e.status<<" / "<<Escape(ReasonName(e.reason))<<"</td></tr>";}s<<"</table><h2>Mudanças no volume convidado</h2><table><tr><th>Seq.</th><th>Operação e recurso</th><th>Tamanho anterior</th><th>Tamanho posterior</th><th>Decisão</th></tr>";
    for(const auto& row:rows_){const auto& e=row.event;if(e.stage!=Stage::Result||e.status||e.decision==Decision::Deny||!(e.flags&Mutation)||(e.domain!=Domain::File&&e.domain!=Domain::Registry))continue;
        s<<"<tr><td>"<<e.sequence<<"</td><td><code>"<<Escape(OperationName(e.operation)+" / "+e.resource+" / "+e.secondary)<<"</code></td><td>"<<e.beforeBytes<<"</td><td>"<<e.afterBytes<<"</td><td>"<<Escape(DecisionName(e.decision))<<"</td></tr>";}
    s<<"</table><p>Confirmação se refere ao padrão observado, sem atribuir identidade de malware. Hashes, autoria e evidências numeradas estão no JSON correspondente. Simulação de arquivos e registro aplica efeitos no volume sintético do convidado; outros domínios recusam simulação.</p></html>\n";return s.str();
}
}
