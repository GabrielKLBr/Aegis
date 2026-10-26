#include "Desktop.h"
namespace guardian::desktop {
uint64_t Core::WindowOrder(uint64_t handle) const noexcept {if(state_)for(const auto& surface:state_->surfaces)if(surface.info.handle==handle)return surface.z;return 0;}
void Core::RaiseWindow(uint64_t handle) noexcept {auto* surface=Find(handle);for(uint32_t n=0;surface&&surface->info.parent&&n<MaxWindows;++n)surface=Find(surface->info.parent);if(surface){surface->z=state_->z++;state_->changed=true;}}
uint32_t Core::InjectWindow(const presentation::Event& event,uint64_t session,uint64_t now) noexcept {
    using presentation::Kind;
    if(!state_||!presentation::ValidEvent(event)||event.session!=session)return 87;
    auto* root=Find(event.window);if(!root||root->info.parent)return 1400;
    const auto belongs=[&](Surface* w) noexcept {for(uint32_t n=0;w&&n<MaxWindows;++n){if(w==root)return true;w=Find(w->info.parent);}return false;};
    auto* focused=Find(state_->focus);
    if(event.kind==Kind::Blur){if(belongs(focused)){for(auto& key:state_->keys)key=0;state_->buttons=state_->modifiers=0;return SetFocus(root->info.pid,0);}return 0;}
    if(!(root->info.flags&1)||(root->info.style&0x08000000))return 5;
    const auto push=[&](Surface* target,uint32_t id,uint64_t wp=0,int64_t lp=0,uint32_t flags=0) noexcept {
        Message message;message.window=target->info.handle;message.id=id;message.wparam=wp;message.lparam=lp;message.time=uint32_t(now/1000000);message.flags=flags;return Push(target->info.pid,target->info.tid,message);
    };
    if(event.kind==Kind::Geometry){auto* queue=Messages(root->info.pid,root->info.tid);if(!queue||queue->count>QueueDepth-2)return 1816;Request r;r.op=Op::Position;r.handle=root->info.handle;r.rect=event.bounds;r.flags=4;return Execute(root->info.pid,root->info.tid,now,r);}
    if(event.kind==Kind::Close)return push(root,0x10);
    if(event.kind==Kind::Focus){RaiseWindow(root->info.handle);return belongs(focused)?0:SetFocus(root->info.pid,root->info.handle);}
    if(event.kind==Kind::Move||event.kind==Kind::Button){
        const auto& client=root->info.client;if(event.x>=client.right-client.left||event.y>=client.bottom-client.top)return 87;
        const auto x=client.left+event.x,y=client.top+event.y;Surface* hit=root;uint32_t bestDepth=0;
        for(auto& candidate:state_->surfaces){if(!candidate.info.handle||!(candidate.info.flags&1)||(candidate.info.style&0x08000000)||!belongs(&candidate))continue;
            uint32_t depth=0;bool eligible=true;for(auto* current=&candidate;current;current=Find(current->info.parent)){if(!(current->info.flags&1)||(current->info.style&0x08000000)||x<current->info.client.left||x>=current->info.client.right||y<current->info.client.top||y>=current->info.client.bottom){eligible=false;break;}if(current==root)break;++depth;}
            if(eligible&&(depth>bestDepth||(depth==bestDepth&&candidate.z>=hit->z))){hit=&candidate;bestDepth=depth;}
        }
        state_->mouseX=x;state_->mouseY=y;state_->modifiers=event.modifiers;
        if(event.kind==Kind::Button&&event.buttons){RaiseWindow(root->info.handle);const auto error=SetFocus(hit->info.pid,hit->info.handle);if(error)return error;}
        const auto keys=(event.buttons&3)|((event.buttons&4)?0x10u:0u)|((event.modifiers&1)?4u:0u)|((event.modifiers&2)?8u:0u);
        const auto lp=int64_t(uint32_t(uint16_t(x-hit->info.client.left))|(uint32_t(uint16_t(y-hit->info.client.top))<<16));
        if(event.kind==Kind::Move)return push(hit,0x200,keys,lp);
        const auto changed=state_->buttons^event.buttons;state_->buttons=event.buttons;const bool down=(event.buttons&changed)!=0;
        const auto message=changed&2?(down?0x204u:0x205u):changed&4?(down?0x207u:0x208u):(down?0x201u:0x202u);return changed?push(hit,message,keys,lp):0;
    }
    if(!belongs(focused)){const auto error=SetFocus(root->info.pid,root->info.handle);if(error)return error;focused=root;}
    for(auto* current=focused;current;current=Find(current->info.parent))if(!(current->info.flags&1)||(current->info.style&0x08000000))return 5;
    if(event.kind==Kind::Text){auto* queue=Messages(focused->info.pid,focused->info.tid);const uint32_t count=event.key>0xFFFF?2u:1u;if(!queue||queue->count>QueueDepth-count)return 1816;if(count==1)return push(focused,0x102,event.key,1);const auto scalar=event.key-0x10000;auto error=push(focused,0x102,0xD800+(scalar>>10),1);return error?error:push(focused,0x102,0xDC00+(scalar&1023),1);}
    if(event.kind==Kind::KeyDown||event.kind==Kind::KeyUp){Input input;input.type=event.kind==Kind::KeyDown?InputType::KeyDown:InputType::KeyUp;input.key=event.key;input.scan=event.scan;input.modifiers=event.modifiers;input.flags=event.flags;return Inject(input,now);}
    return 87;
}
}
