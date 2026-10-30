#include "DesktopBackend.h"
#include "../state/Files.h"
#include <cstring>
#include <iomanip>
#include <sstream>
namespace av::microvm::desktop_session {
using namespace guardian::desktop;
namespace {
std::string Text(const uint16_t* text){std::ostringstream out;out<<'"';for(uint32_t i=0;i<64&&text[i];++i){const auto c=text[i];if(c>=32&&c<=126&&c!='"'&&c!='\\')out<<char(c);else out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<uint32_t(c)<<std::dec;}out<<'"';return out.str();}
std::string Windows(const Frame& frame){std::ostringstream out;out<<'[';for(uint32_t i=0;i<frame.count;++i){if(i)out<<',';const auto& w=frame.windows[i];out<<"{\"handle\":"<<w.handle<<",\"parent\":"<<w.parent<<",\"owner\":"<<w.owner<<",\"pid\":"<<w.pid<<",\"tid\":"<<w.tid<<",\"style\":"<<w.style<<",\"flags\":"<<w.flags<<",\"x\":"<<w.bounds.left<<",\"y\":"<<w.bounds.top<<",\"width\":"<<w.bounds.right-w.bounds.left<<",\"height\":"<<w.bounds.bottom-w.bounds.top<<",\"clientX\":"<<w.client.left<<",\"clientY\":"<<w.client.top<<",\"title\":"<<Text(w.title)<<'}';}out<<']';return out.str();}
}
std::vector<TimedInput> ReadScript(const std::filesystem::path& path){const auto data=state::ReadFile(path,128u<<10);std::istringstream lines(std::string(data.begin(),data.end()));std::vector<TimedInput> result;std::string line;uint64_t previous=0;
    while(std::getline(lines,line)){if(const auto comment=line.find('#');comment!=std::string::npos)line.resize(comment);std::istringstream fields(line);uint64_t ms=0;std::string op,extra;fields>>std::ws;if(fields.eof())continue;state::Require(bool(fields>>ms),"Desktop input timestamp rejected");state::Require(bool(fields>>op)&&ms<=120000&&ms*1000000>=previous,"Desktop input timestamp/order rejected");TimedInput item;item.ns=ms*1000000;previous=item.ns;
        if(op=="down"||op=="up"){item.event.type=op=="down"?InputType::KeyDown:InputType::KeyUp;state::Require(bool(fields>>item.event.key>>item.event.scan>>item.event.modifiers),"Physical key requires VK, scan and modifiers");}
        else if(op=="text"){item.event.type=InputType::Text;state::Require(bool(fields>>item.event.key),"Text requires Unicode scalar");}
        else if(op=="move"||op=="button"){item.event.type=op=="move"?InputType::Move:InputType::Button;state::Require(bool(fields>>item.event.x>>item.event.y>>item.event.buttons),"Mouse requires coordinates and button state");}
        else if(op=="close")item.event.type=InputType::Close;else if(op=="blur")item.event.type=InputType::Blur;else throw std::runtime_error("Unknown desktop input operation");
        state::Require(!(fields>>extra)&&ValidInput(item.event)&&result.size()<2048,"Desktop input rejected");result.push_back(item);
    }return result;
}
Backend::Backend(uint64_t boot,const Options& options):boot_(boot),options_(options),pixels_(PixelCount,0xFF172536){state::Require(boot&&(!options.interactive||options.script.empty()),"Select manual or scripted desktop input");uint64_t previous=0;for(const auto& e:options.script){state::Require(ValidInput(e.event)&&e.ns>=previous&&e.ns<=120000000000ull,"Desktop input policy rejected");previous=e.ns;}state::Require(options.script.size()<=2048,"Desktop script quota exceeded");}
Info Backend::Enumeration() const {Info i;i.boot=boot_;i.interactive=options_.interactive?1u:0u;i.reserved=options_.integrated?1u:0u;return i;}
void Backend::Save(state::Writer& w) const {
    state::Require(!options_.interactive&&!options_.integrated&&options_.script.empty()&&!options_.poll&&!options_.display&&!frames_&&!inputs_&&!completed_,"Checkpoint requires the native desktop without a pixel presenter");
    w.Pod(uint32_t(0x1310));w.Pod(boot_);w.Pod(sequence_);w.Pod(now_);w.Pod(scriptOrigin_);w.Pod(uint32_t(scriptStarted_));
}
void Backend::Load(state::Reader& r){
    state::Writer validate;Save(validate);state::Require(r.Pod<uint32_t>()==0x1310&&r.Pod<uint64_t>()==boot_,"Desktop checkpoint identity mismatch");
    sequence_=r.Pod<uint64_t>();now_=r.Pod<uint64_t>();scriptOrigin_=r.Pod<uint64_t>();auto started=r.Pod<uint32_t>();state::Require(started<=1&&scriptOrigin_<=now_,"Desktop checkpoint clock rejected");scriptStarted_=started!=0;
}
bool Backend::Exchange(Packet& p,const uint32_t* pixels,uint64_t now){if(completed_||p.magic!=Magic||p.boot!=boot_||p.version!=1||p.bytes!=sizeof(p)||p.sequence!=sequence_+1||p.status||now<now_||(p.op!=1&&p.op!=2))return false;now_=now;++sequence_;
    if(p.op==1){if(!scriptStarted_){scriptOrigin_=now;scriptStarted_=true;}p.status=1;p.input={};if(input_<options_.script.size()&&options_.script[input_].ns<=now-scriptOrigin_){p.input=options_.script[input_++].event;delivered_.push_back(now);p.status=0;}else if(options_.interactive&&options_.poll){p.input=options_.poll();if(p.input.type!=InputType::None)p.status=0;}if(!p.status){if(!ValidInput(p.input))return false;++inputs_;capture_=true;}return true;}
    if(!pixels||!ValidFrame(p.frame))return false;frame_=p.frame;std::memcpy(pixels_.data(),pixels,PixelCount*4);hash_=Hash(pixels_.data());++frames_;
    if(history_.size()<4096){std::ostringstream entry;entry<<"{\"frame\":"<<frames_<<",\"ns\":"<<now<<",\"hash\":"<<hash_<<",\"focus\":"<<frame_.focus<<",\"windows\":"<<Windows(frame_)<<'}';history_.push_back(entry.str());}
    if(options_.display)options_.display(frame_,pixels_.data());if(capture_&&captures_<16&&frame_.count){Capture();capture_=false;}return true;
}
void Backend::Capture(){if(options_.output.empty())return;const auto name="frame-"+std::to_string(++captures_);const std::string header="P6\n1280 720\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());ppm.reserve(ppm.size()+PixelCount*3);for(const auto color:pixels_){ppm.push_back(uint8_t(color>>16));ppm.push_back(uint8_t(color>>8));ppm.push_back(uint8_t(color));}state::AtomicWrite(options_.output/(name+".ppm"),ppm);state::AtomicText(options_.output/(name+".json"),"{\"hash\":"+std::to_string(hash_)+",\"windows\":"+Windows(frame_)+"}\n");}
bool Backend::Complete(const Report& r){if(completed_||r.magic!=Magic||r.boot!=boot_||r.version!=1||r.bytes!=sizeof(r)||r.frames!=frames_||r.inputs!=inputs_||r.hash!=hash_||r.liveWindows||r.liveMessages)return false;completed_=true;return true;}
void Backend::Export() const {if(options_.output.empty())return;std::ostringstream out;out<<"{\"schema\":1,\"phase\":\"14.7\",\"width\":1280,\"height\":720,\"frames\":"<<frames_<<",\"inputs\":"<<inputs_<<",\"scriptConsumed\":"<<input_<<",\"completed\":"<<(completed_?"true":"false")<<",\"hash\":"<<hash_<<",\"scriptClock\":\"first-message-poll\",\"scriptOriginNs\":"<<scriptOrigin_<<",\"deliveries\":[";for(size_t i=0;i<delivered_.size();++i){if(i)out<<',';out<<"{\"dueNs\":"<<(scriptOrigin_+options_.script[i].ns)<<",\"deliveredNs\":"<<delivered_[i]<<'}';}out<<"],\"history\":[";for(size_t i=0;i<history_.size();++i){if(i)out<<',';out<<history_[i];}out<<"]}\n";state::AtomicText(options_.output/"desktop.json",out.str());}
}
