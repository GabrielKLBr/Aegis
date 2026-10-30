    std::unique_ptr<guardian::checkpoint::User> savedUser;
    bool restoringUser=false;
    uint32_t Checkpoint(WirePacket& p){
#ifdef GUARDIAN_NATIVE_GDI
        (void)p;return ERROR_NOT_SUPPORTED;
#else
        namespace c=guardian::checkpoint;
        const auto op=uint32_t(p.command.op);
        if(op==c::Begin){
            if(complete||overflow)return ERROR_INVALID_STATE;
            for(const auto& t:timers)if(t.native)return ERROR_BUSY;
            for(const auto& v:pending)if(v.id)return ERROR_BUSY;
            savedUser=std::make_unique<c::User>();auto& s=*savedUser;s.boot=boot;s.session=session;s.sequence=sequence;s.next=next;s.nextCookie=nextCookie;s.nextEvent=nextEvent;std::copy(std::begin(calls),std::end(calls),s.calls);
            if(auto* o=Find(GetFocus()))s.focus=o->id;
            // Parents precede children even when a freed object slot was reused.
            for(uint32_t pass=0;pass<u::MaxWindows;++pass)for(auto& o:objects)if(o.id){bool seen=false,parent=!o.parent;for(uint32_t i=0;i<s.windows;++i){seen|=s.window[i].window.id==o.id;parent|=s.window[i].window.id==o.parent;}if(seen||!parent)continue;
                auto& w=s.window[s.windows++];Describe(o,w.window);w.window.visible=(w.window.style&WS_VISIBLE)!=0;w.window.enabled=(w.window.style&WS_DISABLED)==0;w.control=o.control;
                if(auto* parentWindow=Find(o.parent)){u::Window parentInfo;Describe(*parentWindow,parentInfo);w.window.bounds.left-=parentInfo.originX;w.window.bounds.right-=parentInfo.originX;w.window.bounds.top-=parentInfo.originY;w.window.bounds.bottom-=parentInfo.originY;}
                wchar_t text[512]{};if(GetWindowTextLengthW(o.hwnd)>=512)return ERROR_NOT_SUPPORTED;GetWindowTextW(o.hwnd,text,512);std::wstring value=text;if(!o.parent&&value.starts_with(Prefix))value.erase(0,std::size(Prefix)-1);if(value.size()>255)return ERROR_NOT_SUPPORTED;std::copy(value.begin(),value.end(),w.text);
                if(o.kind==u::Class::Button)w.check=uint32_t(SendMessageW(o.hwnd,BM_GETCHECK,0,0));
                if(o.kind==u::Class::Edit)w.selection=uint32_t(SendMessageW(o.hwnd,EM_GETSEL,0,0));
            }
            s.events=uint32_t(events.size());std::copy(events.begin(),events.end(),s.event);return c::Valid(s)?0:ERROR_NOT_SUPPORTED;
        }
        if(op==c::Load){if(sequence||complete||!events.empty())return ERROR_INVALID_STATE;for(const auto& o:objects)if(o.id)return ERROR_INVALID_STATE;savedUser=std::make_unique<c::User>();return 0;}
        if(op==c::Read||op==c::Write){if(!savedUser||p.command.flags>=sizeof(c::User))return ERROR_INVALID_PARAMETER;const auto offset=p.command.flags;auto count=sizeof(c::User)-offset;if(count>512)count=512;auto* bytes=reinterpret_cast<uint8_t*>(savedUser.get());if(op==c::Read)std::memcpy(p.reply.text,bytes+offset,count);else std::memcpy(bytes+offset,p.command.text,count);return 0;}
        if(op!=c::Commit||!savedUser||!c::Valid(*savedUser)||savedUser->boot!=boot||savedUser->session!=session)return ERROR_INVALID_DATA;
        const auto& s=*savedUser;restoringUser=true;
        for(uint32_t i=0;i<s.windows;++i){const auto& v=s.window[i];u::Packet create;create.pid=v.window.pid;create.tid=v.window.tid;create.command.op=u::Op::Create;create.command.other=v.window.parent;create.command.kind=v.window.kind;create.command.style=v.window.style;create.command.rect=v.window.bounds;create.command.id=v.control;std::copy(std::begin(v.text),std::end(v.text),create.command.text);next=uint32_t(v.window.id);const auto error=Execute(create);if(error||create.reply.value!=v.window.id)return error?error:ERROR_INVALID_DATA;auto* o=Find(v.window.id);EnableWindow(o->hwnd,v.window.enabled!=0);ShowWindow(o->hwnd,v.window.visible?SW_SHOWNOACTIVATE:SW_HIDE);if(o->kind==u::Class::Button)SendMessageW(o->hwnd,BM_SETCHECK,v.check,0);if(o->kind==u::Class::Edit)SendMessageW(o->hwnd,EM_SETSEL,LOWORD(v.selection),HIWORD(v.selection));}
        if(auto* focused=Find(s.focus))SetFocus(focused->hwnd);
        events.assign(s.event,s.event+s.events);next=s.next;nextCookie=s.nextCookie;nextEvent=s.nextEvent;sequence=s.sequence;std::copy(std::begin(s.calls),std::end(s.calls),calls);restoringUser=false;savedUser.reset();return 0;
#endif
    }
