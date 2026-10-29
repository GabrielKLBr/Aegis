// Included after the shared guest volume is declared in Win32Kernel.inl.
bool FrameworkReference(uint32_t pid,const guardian::processes::Token& token,const char* path,char* source) noexcept {
    using namespace guardian;if(!win32Mounted)return false;
    storage::Command open;open.op=storage::Op::Open;memcpy(open.path,path,128);open.access=storage::Read;open.share=7;
    if(win32Storage.Execute(pid,token,open))return false;
    storage::Command read;read.op=storage::Op::Read;read.handle=open.handle;read.length=sizeof(frameworks::FileReference);
    const auto status=win32Storage.Execute(pid,token,read);
    storage::Command size;size.op=storage::Op::FileSize;size.handle=open.handle;const auto sizeStatus=win32Storage.Execute(pid,token,size);
    storage::Command close;close.op=storage::Op::Close;close.handle=open.handle;const auto closeStatus=win32Storage.Execute(pid,token,close);
    if(status||sizeStatus||closeStatus||read.transferred!=sizeof(frameworks::FileReference)||size.transferred!=sizeof(frameworks::FileReference))return false;
    frameworks::FileReference reference;memcpy(&reference,read.data,sizeof(reference));char canonical[128]{};
    if(reference.magic!=frameworks::FileMagic||reference.version!=1||reference.bytes!=sizeof(reference)||!pevolume::Text(reference.source)||!pevolume::Canonical(reference.source,canonical)||!pevolume::Equal(reference.source,canonical)||!pevolume::Prefix(canonical,"c:\\app\\"))return false;
    const auto* header=reinterpret_cast<const pevolume::Header*>(pevolume::SourceGpa);
    for(uint32_t i=0;i<header->files;++i){const auto& file=pevolume::FileAt(*header,i);
        if(file.kind!=pevolume::Kind::Manifest&&pevolume::Equal(file.path,canonical)&&file.bytes==reference.fileBytes&&file.hash==reference.hash){memcpy(source,canonical,128);return true;}
    }return false;
}
