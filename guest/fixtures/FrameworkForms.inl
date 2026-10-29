// Included inside VM. All HWNDs are guest handles issued by the existing USER32
// broker. Managed references/delegate tokens never cross that boundary.
    static constexpr uint32_t MaxGuiMessages=4096,MaxGuiMilliseconds=15000;
    static LRESULT CALLBACK FormsProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* vm=activeVM;
        if(!vm||vm->guiThread!=GetCurrentThreadId())return 0;
        if(message==WM_NCCREATE)return TRUE;
        uint32_t id=0;for(uint32_t i=0;i<vm->objectCount;++i)if(vm->objects[i].peer.window==window){id=i+1;break;}
        if(!id)return DefWindowProcW(window,message,wp,lp);
        auto& peer=vm->objects[id-1].peer;
        if(message==WM_CLOSE){vm->ClosePeer(id);return 0;}
        if(message==WM_DESTROY){peer.closing=true;vm->Disposed(id);return 0;}
        if(message==WM_NCDESTROY){peer.window=nullptr;return 0;}
        if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED&&lp){
            for(uint32_t i=0;i<vm->objectCount;++i){auto& child=vm->objects[i].peer;
                if(child.parent==id&&child.kind==c::FormsKind::Button&&child.window==reinterpret_cast<HWND>(lp)){vm->Fire(i+1,false);break;}}
            return 0;
        }
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);if(dc){FillRect(dc,&paint.rcPaint,GetSysColorBrush(COLOR_BTNFACE));EndPaint(window,&paint);}return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    bool SameDelegate(uint32_t a,uint32_t b){
        if(!a||!b||a>objectCount||b>objectCount)return false;
        const auto& x=objects[a-1];const auto& y=objects[b-1];
        return x.peer.kind==c::FormsKind::Delegate&&y.peer.kind==c::FormsKind::Delegate&&x.fields[0].kind==y.fields[0].kind&&x.fields[0].number==y.fields[0].number&&x.fields[1].number==y.fields[1].number;
    }
    bool Fire(uint32_t id,bool disposed){
        if(image.error!=c::Error::Ok)return false;
        if(!id||id>objectCount)return Error(c::Error::Stack);
        const auto& peer=objects[id-1].peer;const auto count=disposed?peer.disposedCount:peer.clickCount;
        uint32_t handlers[8]{};for(uint32_t i=0;i<count;++i)handlers[i]=disposed?peer.disposedHandlers[i]:peer.clicks[i];
        if(!count)return true;
        if(!eventArgs){if(objectCount>=c::MaxObjects)return Error(c::Error::Quota);eventArgs=++objectCount;objects[eventArgs-1].peer.kind=c::FormsKind::EventArgs;}
        for(uint32_t i=0;i<count;++i){
            if(!handlers[i]||handlers[i]>objectCount)return Error(c::Error::Stack);
            const auto& handler=objects[handlers[i]-1];c::Signature signature;c::Body body;
            if(handler.peer.kind!=c::FormsKind::Delegate||!image.Method(uint32_t(handler.fields[1].number),signature,body))return Error(c::Error::Token);
            Value args[3]{};uint32_t at=0;if(signature.instance)args[at++]=handler.fields[0];
            args[at++]={Kind::Object,id};args[at++]={Kind::Object,eventArgs};Value unused;
            if(!Call(uint32_t(handler.fields[1].number),args,at,unused,guiDepth+1))return false;
        }return true;
    }
    bool Disposed(uint32_t id){auto& peer=objects[id-1].peer;if(peer.disposed)return image.error==c::Error::Ok;
        peer.disposed=true;peer.visible=false;if(guiMain==id)guiExit=true;return Fire(id,true);
    }
    bool ClosePeer(uint32_t id){
        if(!id||id>objectCount)return Error(c::Error::Stack);auto& peer=objects[id-1].peer;
        if(peer.disposed||peer.closing)return true;peer.closing=true;bool ok=true;
        for(uint32_t i=0;i<objectCount;++i)if(objects[i].peer.parent==id&&c::FormsControl(objects[i].peer.kind))if(!ClosePeer(i+1))ok=false;
        if(peer.window&&IsWindow(peer.window)&&!DestroyWindow(peer.window))ok=Error(c::Error::Output);
        peer.window=nullptr;if(!peer.disposed&&!Disposed(id))ok=false;return ok;
    }
    bool CleanupForms(){bool ok=true;for(uint32_t i=0;i<objectCount;++i)if(c::FormsControl(objects[i].peer.kind)&&!ClosePeer(i+1))ok=false;
        if(guiClass){if(!UnregisterClassW(L"Aegis.ManagedForms",GetModuleHandleW(nullptr)))ok=Error(c::Error::Output);guiClass=false;}
        if(activeVM==this)activeVM=nullptr;return ok;
    }
    bool CreatePeer(uint32_t id){
        auto& peer=objects[id-1].peer;if(peer.disposed)return Error(c::Error::UnsupportedCall);
        if(peer.window)return true;const bool form=peer.kind==c::FormsKind::Form;
        if(!c::FormsControl(peer.kind)||(!form&&(!peer.parent||peer.parent>objectCount)))return Error(c::Error::UnsupportedCall);
        if(form&&!guiClass){WNDCLASSW wc{};wc.lpfnWndProc=FormsProcedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"Aegis.ManagedForms";
            if(!RegisterClassW(&wc))return Error(c::Error::Output);guiClass=true;}
        HWND parent=nullptr;if(!form){auto& owner=objects[peer.parent-1].peer;if(!owner.window)return Error(c::Error::Stack);parent=owner.window;}
        const wchar_t* cls=form?L"Aegis.ManagedForms":peer.kind==c::FormsKind::Button?L"BUTTON":peer.kind==c::FormsKind::Label?L"STATIC":L"EDIT";
        DWORD style=form?WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN:WS_CHILD|WS_TABSTOP;
        if(!form&&peer.visible)style|=WS_VISIBLE;if(peer.kind==c::FormsKind::TextBox)style|=WS_BORDER|ES_AUTOHSCROLL;
        peer.window=CreateWindowExW(0,cls,peer.text,style,peer.x,peer.y,peer.width,peer.height,parent,form?nullptr:reinterpret_cast<HMENU>(uintptr_t(id)),GetModuleHandleW(nullptr),nullptr);
        if(!peer.window)return Error(c::Error::Output);
        for(uint32_t i=0;i<objectCount;++i)if(objects[i].peer.parent==id&&c::FormsControl(objects[i].peer.kind)&&!CreatePeer(i+1))return false;
        return true;
    }
    bool PumpForms(uint32_t main){
        if(guiRunning||main&&(!GetObject({Kind::Object,main})||objects[main-1].peer.kind!=c::FormsKind::Form||objects[main-1].peer.disposed))return Error(c::Error::UnsupportedCall);
        guiRunning=true;guiExit=false;guiMain=main;const auto start=GetTickCount64();
        if(main){auto& peer=objects[main-1].peer;if(!CreatePeer(main)){guiRunning=false;return false;}peer.visible=true;ShowWindow(peer.window,SW_SHOW);}
        while(!guiExit&&image.error==c::Error::Ok){
            if(GetTickCount64()-start>=MaxGuiMilliseconds){Error(c::Error::Instructions);break;}
            MSG message{};if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(++guiMessages>MaxGuiMessages){Error(c::Error::Instructions);break;}
                if(message.message==WM_QUIT){guiExit=true;break;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }else Sleep(1);
        }
        guiRunning=false;guiMain=0;return image.error==c::Error::Ok;
    }
    bool FormsCall(c::FormsKind declared,const char* name,const Value* args,uint32_t count,Value& result,uint32_t depth){
        if(!guiThread)guiThread=GetCurrentThreadId();if(guiThread!=GetCurrentThreadId()||activeVM&&activeVM!=this)return Error(c::Error::UnsupportedCall);
        activeVM=this;const auto savedDepth=guiDepth;guiDepth=depth;
        struct Restore {uint32_t& slot;uint32_t previous;~Restore(){slot=previous;}} restore{guiDepth,savedDepth};
        if(declared==c::FormsKind::Application){
            if(c::Equal(name,"Run")){if(count&&args[0].kind==Kind::Null)return Error(c::Error::Null);return PumpForms(count?uint32_t(args[0].number):0);}
            if(c::Equal(name,"Exit")){guiExit=true;bool ok=true;for(uint32_t i=0;i<objectCount;++i)if(objects[i].peer.kind==c::FormsKind::Form&&!ClosePeer(i+1))ok=false;return ok;}
            if(c::Equal(name,"DoEvents")){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(++guiMessages>MaxGuiMessages)return Error(c::Error::Instructions);if(message.message==WM_QUIT){guiExit=true;break;}TranslateMessage(&message);DispatchMessageW(&message);if(image.error!=c::Error::Ok)return false;}return true;}
            return Error(c::Error::UnsupportedCall);
        }
        auto* object=count?GetObject(args[0]):nullptr;if(!object)return Error(c::Error::Stack);auto& peer=object->peer;const auto id=uint32_t(args[0].number);
        if(peer.kind!=declared&&!((declared==c::FormsKind::Control||declared==c::FormsKind::Component)&&c::FormsControl(peer.kind)))return Error(c::Error::Stack);
        if(c::Equal(name,".ctor")){
            if(declared==c::FormsKind::Delegate){
                c::Signature target;c::Body body;if(count!=3||args[2].kind!=Kind::Method||!image.Method(uint32_t(args[2].number),target,body)||target.count!=2||target.result!=c::Type::Void||target.args[0]!=c::Type::Object||target.argClass[0]||image.NativeType(target.argClass[1])!=c::FormsKind::EventArgs)return Error(c::Error::Signature);
                const auto* receiver=GetObject(args[1]);if(target.instance?(!receiver||receiver->type!=image.Owner(6,uint32_t(args[2].number)&0xFFFFFF)):args[1].kind!=Kind::Null)return Error(c::Error::Stack);
                object->fields[0]=args[1];object->fields[1]=args[2];return true;
            }
            if(declared==c::FormsKind::Form){peer.x=80;peer.y=80;peer.width=300;peer.height=300;peer.visible=false;}return true;
        }
        if(c::Equal(name,"get_IsDisposed")){result={Kind::Integer,peer.disposed};return true;}
        if(c::Equal(name,"Dispose")||c::Equal(name,"Close"))return ClosePeer(id);
        if(peer.disposed)return Error(c::Error::UnsupportedCall);
        if(c::Equal(name,"Add")&&declared==c::FormsKind::Collection){
            auto* child=GetObject(args[1]);if(!child||!c::FormsControl(child->peer.kind)||child->peer.kind==c::FormsKind::Form||child->peer.parent||child->peer.disposed||!peer.parent||peer.parent>objectCount)return Error(c::Error::UnsupportedCall);
            if(objects[peer.parent-1].peer.disposed)return Error(c::Error::UnsupportedCall);
            for(uint32_t ancestor=peer.parent,n=0;ancestor&&n<c::MaxObjects;++n){if(ancestor==uint32_t(args[1].number))return Error(c::Error::UnsupportedCall);ancestor=objects[ancestor-1].peer.parent;}
            child->peer.parent=peer.parent;return !objects[peer.parent-1].peer.window||CreatePeer(uint32_t(args[1].number));
        }
        if(c::Equal(name,"get_Controls")){
            if(!peer.collection){if(objectCount>=c::MaxObjects)return Error(c::Error::Quota);peer.collection=++objectCount;auto& collection=objects[peer.collection-1].peer;collection.kind=c::FormsKind::Collection;collection.parent=id;}
            result={Kind::Object,peer.collection};return true;
        }
        const bool eventAdd=c::Equal(name,"add_Click")||c::Equal(name,"add_Disposed"),eventRemove=c::Equal(name,"remove_Click")||c::Equal(name,"remove_Disposed");
        if(eventAdd||eventRemove){
            const bool disposed=c::Equal(name,"add_Disposed")||c::Equal(name,"remove_Disposed");auto* handler=GetObject(args[1]);if(args[1].kind==Kind::Null)return true;
            if(!disposed&&peer.kind!=c::FormsKind::Button)return Error(c::Error::UnsupportedCall);
            if(!handler||handler->peer.kind!=c::FormsKind::Delegate)return Error(c::Error::Stack);auto* handlers=disposed?peer.disposedHandlers:peer.clicks;auto& n=disposed?peer.disposedCount:peer.clickCount;
            if(eventAdd){if(n>=8)return Error(c::Error::Quota);handlers[n++]=uint32_t(args[1].number);}else{for(uint32_t i=n;i;--i)if(SameDelegate(handlers[i-1],uint32_t(args[1].number))){for(uint32_t j=i;j<n;++j)handlers[j-1]=handlers[j];--n;break;}}return true;
        }
        if(c::Equal(name,"get_Text")){wchar_t text[256]{};uint32_t length=0;if(peer.window){const auto n=GetWindowTextW(peer.window,text,256);if(n<0)return Error(c::Error::Output);length=uint32_t(n);}else{while(peer.text[length]){text[length]=peer.text[length];++length;}}return NewText(reinterpret_cast<const uint16_t*>(text),length,result);}
        if(c::Equal(name,"set_Text")){uint16_t text[c::MaxChars]{};uint32_t length=0;if(!TextValue(args[1],text,length))return false;if(length>255)return Error(c::Error::Quota);for(uint32_t i=0;i<length;++i){if(!text[i]||text[i]<32&&text[i]!='\t')return Error(c::Error::UnsupportedCall);peer.text[i]=wchar_t(text[i]);}peer.text[length]=0;return !peer.window||SetWindowTextW(peer.window,peer.text)||Error(c::Error::Output);}
        if(c::Equal(name,"get_Visible")){bool visible=peer.visible;for(uint32_t parent=peer.parent,n=0;parent&&n<c::MaxObjects;++n){visible=visible&&objects[parent-1].peer.visible;parent=objects[parent-1].peer.parent;}result={Kind::Integer,visible};return true;}
        if(c::Equal(name,"Show")||c::Equal(name,"Hide")||c::Equal(name,"set_Visible")){
            peer.visible=c::Equal(name,"Show")||(c::Equal(name,"set_Visible")&&args[1].number!=0);if(peer.visible&&!CreatePeer(id))return false;if(peer.window)ShowWindow(peer.window,peer.visible?SW_SHOW:SW_HIDE);return true;
        }
        if(c::Equal(name,"PerformClick")){if(peer.kind!=c::FormsKind::Button)return Error(c::Error::UnsupportedCall);if(peer.window)SendMessageW(peer.window,BM_CLICK,0,0);else return Fire(id,false);return image.error==c::Error::Ok;}
        const char* properties[]={"Left","Top","Width","Height"};int* slots[]={&peer.x,&peer.y,&peer.width,&peer.height};
        for(uint32_t i=0;i<4;++i){char getter[24]="get_",setter[24]="set_";for(uint32_t j=0;properties[i][j];++j)getter[4+j]=setter[4+j]=properties[i][j];
            if(c::Equal(name,getter)){result={Kind::Integer,*slots[i]};return true;}
            if(c::Equal(name,setter)){const auto value=int32_t(args[1].number);if(value<(i<2?0:1)||value>(i<2?8192:1024))return Error(c::Error::Quota);*slots[i]=value;return !peer.window||MoveWindow(peer.window,peer.x,peer.y,peer.width,peer.height,TRUE)||Error(c::Error::Output);}
        }
        return Error(c::Error::UnsupportedCall);
    }
