#include "NativeGuiCore.h"
namespace av::microvm::native_gui {
namespace n=guardian::nativegui;
Core::Object* Core::Find(uint64_t id,n::Kind kind,uint32_t pid,uint32_t tid){for(auto& o:objects_)if(o.id==id&&o.id&&o.kind==kind&&o.pid==pid&&o.tid==tid)return &o;return nullptr;}
bool Core::Exchange(n::Packet& p){
    if(complete_||p.magic!=n::Magic||p.boot!=boot_||p.session!=session_||!boot_||!session_||p.version!=n::Version||p.bytes!=sizeof(p)||p.sequence!=sequence_+1||!p.pid||!p.tid||!n::Empty(&p.reply,sizeof(p.reply))||sequence_==n::MaxCommands)return false;
    ++sequence_;auto& c=p.command;auto& r=p.reply;
    const auto result=[&](uint32_t status){r.status=status;for(const auto& o:objects_)if(o.id)++r.live[uint32_t(o.kind)-1];r.completed=complete_?1u:0u;return true;};
    if(!n::Valid(c))return result(uint32_t(c.op)>uint32_t(n::Op::Finish)?50u:87u);
    Object* target=nullptr;Object* extra=nullptr;Object* free=nullptr;n::Kind kind{};
    if(c.op==n::Op::Window||c.op==n::Op::Brush||c.op==n::Op::Dc){
        kind=c.op==n::Op::Window?n::Kind::Window:c.op==n::Op::Brush?n::Kind::Brush:n::Kind::Dc;
        uint32_t windows=0;for(auto& o:objects_){if(!o.id&&!free)free=&o;if(o.id&&o.kind==n::Kind::Window)++windows;}
        if(!free||counter_==4096||(kind==n::Kind::Window&&windows==n::MaxWindows))return result(1816);
    }
    if(c.op==n::Op::Dc||c.op==n::Op::DestroyWindow)target=Find(c.target,n::Kind::Window,p.pid,p.tid);
    else if(c.op==n::Op::Fill||c.op==n::Op::Pixel||c.op==n::Op::ReleaseDc)target=Find(c.target,n::Kind::Dc,p.pid,p.tid);
    else if(c.op==n::Op::DeleteBrush)target=Find(c.target,n::Kind::Brush,p.pid,p.tid);
    if(c.target&&!target)return result(6);
    if(c.op==n::Op::Fill){extra=Find(c.object,n::Kind::Brush,p.pid,p.tid);if(!extra)return result(6);}
    if(c.op==n::Op::Fill||c.op==n::Op::Pixel){auto* window=Find(target->parent,n::Kind::Window,p.pid,p.tid);if(!window||c.rect.right>window->bounds.right-window->bounds.left||c.rect.bottom>window->bounds.bottom-window->bounds.top)return result(87);}
    if(c.op==n::Op::DestroyWindow)for(const auto& o:objects_)if(o.id&&o.parent==target->id)return result(170);
    if(c.op==n::Op::Finish){for(const auto& o:objects_)if(o.id)return result(170);complete_=true;return result(0);}
    if(c.op==n::Op::Hello)return result(50);
    const auto id=free?(uint64_t(kind)<<56)|((session_&0xFFFFFF)<<32)|(++counter_):0;uint64_t value=0;
    const auto status=execute_(c,id,value);if(status)return result(status);
    if(free){*free={id,c.op==n::Op::Dc?c.target:0,p.pid,p.tid,kind,c.rect};r.value=id;}
    else if(c.op==n::Op::ReleaseDc||c.op==n::Op::DeleteBrush||c.op==n::Op::DestroyWindow)*target={};
    else if(c.op==n::Op::Pixel){if(value>0xFFFFFF)return false;r.value=value;}
    return result(0);
}
}
