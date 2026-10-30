#include "Unified.h"
#include "../boot/KernelBoot.h"
#include "../gui/NativeGuiBroker.h"
#include "../gui/NativeGuiManifest.h"
#include "../state/Files.h"
#include <cstring>
#include "guardian/Security.h"
namespace av::microvm::runtime {
using namespace scan;
namespace {
struct ReadLocks {std::vector<HANDLE> handles;~ReadLocks(){for(auto h:handles)CloseHandle(h);}void Add(const fs::path& path){const auto h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);Require(h!=INVALID_HANDLE_VALUE,"Cannot lock runtime input against replacement");handles.push_back(h);}};
fs::path Member(const fs::path& root,const std::string& name){
    const auto part=Utf8Path(name);Require(!name.empty()&&name.size()<256&&name.find(':')==name.npos&&!part.is_absolute()&&!part.has_root_name(),"Runtime member path rejected");
    fs::path value=root;for(const auto& component:part){Require(component!=L".."&&component!=L"."&&!component.empty(),"Runtime traversal rejected");value/=component;const auto status=fs::symlink_status(value);Require(!fs::is_symlink(status),"Runtime links rejected");
        const auto attributes=GetFileAttributesW(value.c_str());Require(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Runtime reparse point rejected");}
    return value;
}
std::vector<uint8_t> Checked(const fs::path& root,const Json& files,const std::string& name,uint64_t maximum){auto bytes=Read(Member(root,name),maximum);Require(Digest(bytes)==files.at(name).get<std::string>(),"Runtime member SHA-256 mismatch: "+name);return bytes;}
void AtomicJson(const fs::path& path,const Json& value){state::AtomicText(path,value.dump(2)+"\n");}
}
Json Run(const fs::path& input,const fs::path& registry,const fs::path& output,const guardian::telemetry::Policy& policy,uint32_t milliseconds,uint64_t checkpointAfter,const fs::path& restore){
    bool ownsOutput=false;
    Json result={{"schema",1},{"type","sandbox"},{"backend","MiniNT-unified-14.13"},{"complete",false},{"classification","inconclusive"},{"loaded",false},{"ran",false},{"faulted",false},{"unresolved",Json::array()},{"events",Json::array()},{"virtualFiles",Json::array()},{"issues",Json::array()},{"inputExecutedOnHost",false},{"coverage","instrumented guest operations and mediated native calls; DLL-local functions are not all observed"}};
    try{
        Require(guardian::telemetry::ValidPolicy(policy)&&milliseconds>=100&&milliseconds<=60000&&checkpointAfter<=guardian::nativeuser::MaxCommands,"Runtime policy/deadline rejected");
        Require(!registry.empty(),"Configure sandboxRuntime with an enrolled immutable runtime registry");OrdinaryDirectory(registry);Require(!fs::exists(output),"New runtime session output required");Require(fs::create_directories(output),"Cannot create runtime output");ownsOutput=true;
        const auto payload=Read(input,8ull<<20);const auto hash=Digest(payload);result["sha256"]=hash;
        ReadLocks locks;locks.Add(registry/"index.json");const auto index=ReadJson(registry/"index.json");Require(index.value("schema",0)==1&&index.value("phase",std::string())=="14.13"&&index.at("applications").is_object()&&index.at("applications").size()<=128,"Runtime registry rejected");
        Require(index["applications"].contains(hash),"Input has no enrolled dependency package; host/legacy fallback is disabled");const auto record=index["applications"][hash];
        const auto recordText=record.dump();const auto recordHash=Digest({reinterpret_cast<const uint8_t*>(recordText.data()),recordText.size()});
        Require(record.at("profile")=="win10"&&(record.at("architecture")=="x86"||record.at("architecture")=="x64"),"Unsupported runtime profile/architecture");const auto mode=record.at("mode").get<std::string>();Require(mode=="user32"||mode=="frameworks"||mode=="compat32","Runtime family rejected");
        const auto& files=record.at("files");Require(files.is_object()&&files.size()<=12,"Runtime file quota");
        for(auto item=files.begin();item!=files.end();++item)locks.Add(Member(registry,item.key()));
        const auto kernelPath=record.at("kernel").get<std::string>(),helperPath=record.at("helper").get<std::string>(),volumePath=record.at("volume").get<std::string>(),manifestPath=record.at("manifest").get<std::string>(),bindingPath=record.at("binding").get<std::string>();
        Checked(registry,files,kernelPath,8<<20);const auto helperBytes=Checked(registry,files,helperPath,16<<20);const auto volume=Checked(registry,files,volumePath,guardian::pevolume::MaxBytes);const auto manifestBytes=Checked(registry,files,manifestPath,4<<20);Checked(registry,files,bindingPath,176);
        const auto manifest=Json::parse(manifestBytes);Require(manifest.at("architecture")==record.at("architecture")&&manifest.at("profile")==record.at("profile"),"Runtime manifest architecture/profile mismatch");
        const auto kernel=boot::KernelImage::Read(Member(registry,kernelPath));const auto binding=compatibility::Read(Member(registry,bindingPath),Member(registry,manifestPath));compatibility::Verify(binding,kernel.FileBytes(),payload);
        Require(binding.profile==1&&guardian::pevolume::Valid(volume.data(),uint32_t(volume.size())),"Runtime volume/profile rejected");const auto& header=*reinterpret_cast<const guardian::pevolume::Header*>(volume.data());const auto& root=guardian::pevolume::FileAt(header,header.root);Require(root.bytes==payload.size()&&!std::memcmp(volume.data()+root.offset,payload.data(),payload.size()),"Enrolled payload differs from scanner copy");
        const auto volumeHash=compatibility::Sha256(volume);Require(!std::memcmp(volumeHash.data(),binding.runtimeSha,32),"Runtime volume binding rejected");
        const auto native=native_gui::ReadManifest({reinterpret_cast<const char*>(manifestBytes.data()),manifestBytes.size()});Require(native.helperSha256==Digest(helperBytes)&&native.protocol==(mode=="frameworks"?"7":"2")&&native.phase==(mode=="frameworks"?"14.11":mode=="compat32"?"14.12":"14.10B"),"Runtime native backend binding rejected");
        Require((mode=="compat32")==kernel.SupportsCompat32()&&(mode!="frameworks"||kernel.SupportsFrameworks()),"Runtime kernel family mismatch");
        Require(!checkpointAfter||mode!="frameworks","Live GDI/audio checkpoints are unsupported; USER32 basic is required");
        const auto controller=Digest(Read(Executable(),64<<20));result["controllerSha256"]=controller;
        const auto policyJson=Json{{"version",policy.version},{"denyMask",policy.denyMask},{"simulateMask",policy.simulateMask},{"eventLimit",policy.eventLimit}};
        uint64_t sessionId=0;std::unique_ptr<boot::SessionSnapshot> saved;
        if(!restore.empty()){
            const auto receipt=ReadJson(restore,32768);Require(receipt.at("schema")==1&&receipt.at("runtimeIdentity")==recordHash&&receipt.at("applicationSha256")==hash&&receipt.at("policy")==policyJson&&receipt.at("controllerSha256")==controller,"Recovery runtime/application/policy/controller mismatch");const auto name=receipt.at("snapshot").get<std::string>();Require(fs::path(name).filename()==fs::path(name),"Recovery snapshot path rejected");const auto bytes=state::ReadFile(restore.parent_path()/name,600ull<<20);Require(Digest(bytes)==receipt.at("snapshotSha256").get<std::string>(),"Recovery snapshot SHA-256 mismatch");saved=std::make_unique<boot::SessionSnapshot>(boot::SessionSnapshot::Decode(bytes));sessionId=receipt.at("nativeSession").get<uint64_t>();Require(sessionId!=0,"Recovery native session missing");
        }
        // External bundles supply data and guest code, never a host executable.
        const auto trustedHelper=Executable().parent_path()/(mode=="frameworks"?"guardian-audio-host.exe":"guardian-native-user-host.exe");locks.Add(trustedHelper);Require(Digest(Read(trustedHelper,16<<20))==Digest(helperBytes),"Installed trusted helper differs from the enrolled runtime");
        native_gui::Broker broker(trustedHelper,1,sessionId,mode!="frameworks",mode=="frameworks",mode=="frameworks",mode=="frameworks",mode=="frameworks",mode=="frameworks");
        desktop_session::Options desktop;desktop.output=output;desktop.telemetryCapacity=policy.eventLimit;desktop.nativeInfo=broker.Enumeration();desktop.nativeCompleted=[&]{return broker.Completed()||broker.CompleteUnused();};
        if(mode=="frameworks"){desktop.nativeGdi=desktop.nativeControls=desktop.expandedGdi=desktop.softwareGraphics=desktop.audio=true;desktop.gdiExchange=[&](guardian::nativegdi::Packet& p){return broker.Exchange(p);};}
        else {desktop.nativeUser32=true;desktop.userExchange=[&](guardian::nativeuser::Packet& p){return broker.Exchange(p);};desktop.nativeSave=[&]{return broker.CaptureUser();};desktop.nativeLoad=[&](std::span<const uint8_t> data){broker.RestoreUser(data);};}
        boot::CpuOptions cpu;cpu.enabled=cpu.processes=cpu.graphics=true;boot::KernelBootSession vm;vm.Prepare(kernel,256ull<<20,1,cpu,{},{},{},nullptr,{},nullptr,nullptr,&policy,0,{},&binding,volume,{},desktop);if(saved)vm.Restore(*saved);if(checkpointAfter)vm.PauseAtNativeRequest(checkpointAfter);
        const auto run=vm.Run(std::chrono::milliseconds(milliseconds));boot::WriteBootReport(output/"boot.json",{run});result["console"]=run.console;result["exitCode"]=run.stopStatus;result["faulted"]=run.memoryFault||run.cpu.fatal||run.processes.faulted;result["output"]=PathText(output);result["runtimeIdentity"]=recordHash;result["profile"]=record["profile"];result["architecture"]=record["architecture"];result["policy"]=policyJson;result["modules"]=manifest["files"];result["restored"]=bool(saved);
        if(run.paused){auto snapshot=vm.Capture();const auto encoded=snapshot.Encode();state::AtomicWrite(output/"recovery.gns",encoded);AtomicJson(output/"recovery.json",{{"schema",1},{"runtimeIdentity",recordHash},{"controllerSha256",controller},{"applicationSha256",hash},{"policy",policyJson},{"nativeSession",desktop.nativeInfo.session},{"snapshot","recovery.gns"},{"snapshotSha256",Digest(encoded)}});result["checkpointCreated"]=true;result["state"]="paused";Require(broker.CloseCheckpoint(),"Cannot release original checkpoint GUI resources");}
        const auto* trace=vm.Analysis();Require(trace!=nullptr,"Unified runtime requires analysis telemetry");auto analysis=Json::parse(trace->Json());AtomicJson(output/"analysis.json",analysis);result["findings"]=analysis["findings"];result["stateDifferences"]=analysis["stateDifferences"];
        for(const auto& event:analysis["events"]){if(event.value("domain","")=="module"&&event.value("operation","")=="load"&&event.value("stage","")=="result"&&event.value("resource","")==std::string(root.path)&&event.value("status",1)==0)result["loaded"]=true;if(event.value("actorPid",0)&&event.value("actorTid",0))result["ran"]=true;}
        for(auto event:analysis["events"]){event["action"]=event.value("domain","")+std::string(".")+event.value("operation","");event["target"]=event.value("resource","");event["detail"]=std::to_string(event.value("status",0));result["events"].push_back(std::move(event));}
        result["virtualFileScope"]="changes";for(const auto& difference:analysis["stateDifferences"])if(difference.value("domain","")=="file"){auto value=difference;value["path"]=difference.value("resource","");result["virtualFiles"].push_back(std::move(value));}
        result["complete"]=run.success&&analysis.value("complete",false)&&!run.paused;
        if(result["complete"].get<bool>()){result["classification"]="unknown";for(const auto& finding:analysis["findings"]){const auto c=finding.value("classification","");if(c=="detected")result["classification"]="detected";else if(c=="suspicious"&&result["classification"]!="detected")result["classification"]="suspicious";}state::AtomicWrite(output/"security-state.bin",{static_cast<const uint8_t*>(vm.Machine().RamData())+guardian::security::StateGpa,65536});}
        broker.CompleteUnused();if(!broker.Completed()&&mode!="frameworks")broker.CloseCheckpoint();broker.Finish(output);
        if(!broker.Completed()){result["complete"]=false;result["classification"]="inconclusive";}
        if(!result["complete"].get<bool>()&&!run.paused){result["error"]=run.error;result["issues"].push_back("Execution failed or coverage is incomplete; no safe verdict");}
    }catch(const std::exception& e){result["complete"]=false;result["classification"]="inconclusive";result["error"]=e.what();result["issues"].push_back(e.what());}
    if(ownsOutput)AtomicJson(output/"runtime.json",result);return result;
}
}
