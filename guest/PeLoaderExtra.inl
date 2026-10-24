// Included by PeLoader.cpp: shared bounded parsing, no host API or allocation.
bool Loader::Thunks(Module& m,uint32_t dep,uint32_t lookup,uint32_t iat,uint32_t limit) noexcept {
    if((lookup&7)||(iat&7))return Fail(Error::InvalidImage);
    for(uint32_t i=0;i<limit;++i){
        const uint64_t from=uint64_t(lookup)+uint64_t(i)*8,to=uint64_t(iat)+uint64_t(i)*8;
        if(from>UINT32_MAX||to>UINT32_MAX)return Fail(Error::InvalidImage);
        uint64_t thunk=0,address=0;
        if(!Read(m,static_cast<uint32_t>(from),&thunk,8)||to<m.headers||
            !Span(m,static_cast<uint32_t>(to),8)||Span(m,static_cast<uint32_t>(to),8,ReadFlag|ExecuteFlag))return Fail(Error::InvalidImage);
        if(!thunk)return true;
        if(thunk>>63){if(thunk&0x7FFFFFFFFFFF0000ull)return Fail(Error::InvalidImage);
            if(!Symbol(modules_[dep],nullptr,static_cast<uint16_t>(thunk),address))return false;
        }else{if(thunk>UINT32_MAX-2)return Fail(Error::InvalidImage);char text[128]{};
            if(!String(m,static_cast<uint32_t>(thunk)+2,text,sizeof(text))||!text[0])return Fail(Error::InvalidImage);
            if(!Symbol(modules_[dep],text,0,address))return false;
        }
        if(!Write(m,static_cast<uint32_t>(to),&address,8))return false;++m.imports;
    }return Fail(Error::Limit);
}
bool Loader::DelayImports(Module& m) noexcept {
    const auto& d=m.directories[13];if(!d.bytes)return true;
    if(!peprofile::Extended)return Fail(Error::Unsupported);
    // Eagerly bind the delay IAT before sealing. The helper then observes an
    // already resolved address. Bound/unload tables are retained as metadata;
    // unload/rebinding helpers are not advertised by this contract.
    for(uint32_t offset=0;offset+32<=d.bytes;offset+=32){
        uint8_t bytes[32]{};if(!Read(m,d.rva+offset,bytes,32))return false;
        bool zero=true;for(auto b:bytes)if(b)zero=false;if(zero)return true;
        if(U32(bytes)!=1||!U32(bytes+4)||!U32(bytes+8)||!U32(bytes+12)||!U32(bytes+16))return Fail(Error::Unsupported);
        char name[128]{};if(!String(m,U32(bytes+4),name,sizeof(name))||!Name(name))return Fail(Error::InvalidImage);
        uint32_t source=UINT32_MAX;for(uint32_t i=0;i<count_;++i)if(&modules_[i]==&m)source=i;
        const auto dep=Find(name,source<count_?sources_[source].name:nullptr);if(dep==UINT32_MAX)return Fail(Error::MissingModule);
        if(!LoadOne(dep))return false;if(!modules_[dep].dll)return Fail(Error::InvalidImage);
        m.dependencies|=1u<<dep;
        if(!Thunks(m,dep,U32(bytes+16),U32(bytes+12),peprofile::Imports))return false;
        const auto handle=modules_[dep].base;
        if(U32(bytes+8)<m.headers||!Span(m,U32(bytes+8),8,ReadFlag|WriteFlag)||!Write(m,U32(bytes+8),&handle,8))return Fail(Error::InvalidImage);
    }return Fail(Error::InvalidImage);
}
bool Loader::ResourceTree(Module& m,uint32_t offset,uint32_t depth,uint32_t& budget) noexcept {
    const auto& d=m.directories[2];if(depth>2||!budget--||!Range(offset,16,d.bytes))return Fail(Error::InvalidImage);
    uint8_t header[16]{};if(!Read(m,d.rva+offset,header,16))return false;
    const uint32_t named=U16(header+12),ids=U16(header+14),count=named+ids;
    if(count>256||!Range(offset+16,count*8,d.bytes))return Fail(Error::Limit);
    uint32_t previous=0;
    for(uint32_t i=0;i<count;++i){uint8_t entry[8]{};if(!Read(m,d.rva+offset+16+i*8,entry,8))return false;
        const auto key=U32(entry),value=U32(entry+4);
        if(i<named){if(!(key>>31))return Fail(Error::InvalidImage);const auto string=key&0x7FFFFFFF;
            uint16_t length=0;if(!Range(string,2,d.bytes)||!Read(m,d.rva+string,&length,2)||!length||length>128||!Range(string+2,uint32_t(length)*2,d.bytes))return Fail(Error::InvalidImage);
        }else {if(key>>31||key>65535||(i>named&&key<=previous))return Fail(Error::InvalidImage);previous=key;}
        if(depth<2){if(!(value>>31)||!ResourceTree(m,value&0x7FFFFFFF,depth+1,budget))return false;}
        else {if(value>>31||!Range(value,16,d.bytes))return Fail(Error::InvalidImage);uint8_t leaf[16]{};
            if(!Read(m,d.rva+value,leaf,16)||U32(leaf+12)||!U32(leaf)||!U32(leaf+4)||!Span(m,U32(leaf),U32(leaf+4)))return Fail(Error::InvalidImage);}
    }return true;
}
bool Loader::ResourceEntry(Module& m,uint32_t type,uint32_t id,uint32_t language,uint32_t& rva,uint32_t& bytes) noexcept {
    rva=bytes=0;const auto& d=m.directories[2];if(!d.bytes)return Fail(Error::MissingSymbol);
    uint32_t offset=0;const uint32_t keys[]{type,id,language};
    for(uint32_t depth=0;depth<3;++depth){uint8_t h[16]{};if(!Read(m,d.rva+offset,h,16))return false;
        const uint32_t named=U16(h+12),count=named+U16(h+14);bool found=false;
        for(uint32_t i=named;i<count;++i){uint8_t e[8]{};if(!Read(m,d.rva+offset+16+i*8,e,8))return false;
            if(U32(e)!=keys[depth]&&!(depth==2&&language==UINT32_MAX))continue;
            const auto next=U32(e+4);if(depth<2){if(!(next>>31))return Fail(Error::InvalidImage);offset=next&0x7FFFFFFF;}
            else {uint8_t leaf[16]{};if(next>>31||!Range(next,16,d.bytes)||!Read(m,d.rva+next,leaf,16))return Fail(Error::InvalidImage);
                rva=U32(leaf);bytes=U32(leaf+4);return Span(m,rva,bytes)||Fail(Error::InvalidImage);}
            found=true;break;
        }if(!found)return Fail(Error::MissingSymbol);
    }return false;
}
bool Loader::Resources(Module& m,bool activate) noexcept {
    if(!m.directories[2].bytes)return true;
    if(!peprofile::Extended)return Fail(Error::Unsupported);
    uint32_t budget=256;if(!ResourceTree(m,0,0,budget))return false;
#ifdef GUARDIAN_SCALABLE_PE
    if(activate&&namespace_.manifest){uint32_t rva=0,bytes=0;
        if(!ResourceEntry(m,24,m.dll?2u:1u,UINT32_MAX,rva,bytes)){
            if(error_==Error::MissingSymbol){error_=Error::Ok;return true;}return false;}
        if(bytes>sizeof(manifestBytes_))return Fail(Error::Limit);
        if(!Read(m,rva,manifestBytes_,bytes)||!namespace_.manifest(namespace_.context,manifestBytes_,bytes))return Fail(Error::Unsupported);
    }
#else
    (void)activate;
#endif
    return true;
}
bool Loader::Resource(uint32_t index,uint32_t type,uint32_t id,uint32_t language,uint64_t& address,uint32_t& bytes) noexcept {
    address=0;bytes=0;if constexpr(!peprofile::Extended){(void)index;(void)type;(void)id;(void)language;return Fail(Error::Unsupported);}else {
    if(!Get(index)||type>65535||id>65535||(language>65535&&language!=UINT32_MAX))return Fail(Error::Unsupported);
    uint32_t rva=0;if(!ResourceEntry(modules_[index],type,id,language,rva,bytes))return false;
    address=modules_[index].base+rva;error_=Error::Ok;return true;}
}
