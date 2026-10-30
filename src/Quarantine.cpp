#include "Quarantine.h"
#ifdef GUARDIAN_QUARANTINE
#include "quarantine/Store.h"
#endif
namespace av {
Quarantine::Quarantine(std::wstring dir):dir_(std::move(dir)){}
bool Quarantine::Isolate(const Detection& det){
#ifdef GUARDIAN_QUARANTINE
    try {if(det.sha256.empty())return false;quarantine::Store store(dir_);return store.Isolate(scan::fs::absolute(det.file).lexically_normal(),det.sha256,WideToUtf8(det.threat),WideToUtf8(det.reason)).value("complete",false);}catch(const std::exception& e){PrintLine(Utf8ToWide(e.what()),Color::Red);return false;}
#else
    (void)det;return false;
#endif
}
void Quarantine::List() const {
#ifdef GUARDIAN_QUARANTINE
    try{quarantine::Store store(dir_);PrintLine(Utf8ToWide(store.List().dump(2)),Color::Gray);}catch(const std::exception& e){PrintLine(Utf8ToWide(e.what()),Color::Red);}
#else
    PrintLine(L"Transactional quarantine is unavailable in this build.",Color::Red);
#endif
}
}
