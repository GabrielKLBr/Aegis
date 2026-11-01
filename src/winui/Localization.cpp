#include <aegis/localization.hpp>
#include <Windows.h>
#include <string_view>
#include <stdexcept>

namespace aegis::embedded { std::string_view languages() noexcept; }
namespace aegis {
namespace {
const nlohmann::json& resources() {
    static const auto value=nlohmann::json::parse(embedded::languages());
    return value;
}
const nlohmann::json& language(std::string_view locale) {
    for (const auto& entry:resources()) if (entry.at("locale").get_ref<const std::string&>()==locale) return entry.at("translations");
    throw std::invalid_argument("Language is not embedded");
}
const nlohmann::json* lookup(std::string_view locale,const std::string& key) {
    const auto& selected=language(locale);
    if (selected.contains(key)) return &selected.at(key);
    const auto& fallback=language("en-US");
    return fallback.contains(key)?&fallback.at(key):nullptr;
}
std::string substitute(const std::string& text,const std::map<std::string,std::string>& parameters) {
    std::string result;
    for (std::size_t i=0;i<text.size();) {
        if (text[i]!='{') { result+=text[i++]; continue; }
        const auto end=text.find('}',i);
        if (end==std::string::npos) throw std::invalid_argument("Invalid translation placeholder");
        const auto name=text.substr(i+1,end-i-1);
        const auto entry=parameters.find(name);
        if (entry==parameters.end()) throw std::invalid_argument("Missing translation parameter: "+name);
        result+=entry->second;
        i=end+1;
    }
    return result;
}
std::string regional_number(long long count,const std::string& locale) {
    const std::wstring name(locale.begin(),locale.end());
    const auto input=std::to_wstring(count);
    NUMBERFMTW number{};
    wchar_t decimal[8]{},thousand[8]{},grouping[16]{};
    GetLocaleInfoEx(name.c_str(),LOCALE_SDECIMAL,decimal,8);
    GetLocaleInfoEx(name.c_str(),LOCALE_STHOUSAND,thousand,8);
    GetLocaleInfoEx(name.c_str(),LOCALE_SGROUPING,grouping,16);
    number.NumDigits=0; number.LeadingZero=1; number.Grouping=grouping[0]==L'3'?3:0;
    number.lpDecimalSep=decimal; number.lpThousandSep=thousand; number.NegativeOrder=1;
    wchar_t buffer[128]{};
    if (!GetNumberFormatEx(name.c_str(),0,input.c_str(),&number,buffer,128)) return std::to_string(count);
    const auto length=WideCharToMultiByte(CP_UTF8,0,buffer,-1,nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(length),0);
    WideCharToMultiByte(CP_UTF8,0,buffer,-1,result.data(),length,nullptr,nullptr);
    result.resize(result.size()-1);
    return result;
}
}
Localization::Localization(std::string locale) { set_locale(std::move(locale)); }
void Localization::set_locale(std::string locale) {
    if (locale.empty() || locale=="system") {
        wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
        GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH);
        const std::wstring_view system(name);
        locale=system.starts_with(L"pt")?"pt-BR":system.starts_with(L"es")?"es-ES":"en-US";
    }
    // Unknown saved locales fall back to English rather than preventing startup.
    try { (void)language(locale); locale_=std::move(locale); }
    catch (const std::invalid_argument&) { locale_="en-US"; }
}
std::string Localization::locale() const { return locale_; }
std::string Localization::text(std::string key) const {
    const auto* value=lookup(locale_,key);
    if (!value) return key;
    return value->is_string()?value->get<std::string>():value->at("other").get<std::string>();
}
std::string Localization::format(std::string key,const std::map<std::string,std::string>& parameters) const {
    return substitute(text(std::move(key)),parameters);
}
std::string Localization::plural(std::string key,long long count) const {
    const auto* value=lookup(locale_,key);
    if (!value) return key;
    if (!value->is_object()) return format(std::move(key),{{"count",regional_number(count,locale_)}});
    const auto singular=count==1 || count==-1 || (locale_=="pt-BR" && count==0);
    return substitute(value->at(singular?"one":"other").get<std::string>(),{{"count",regional_number(count,locale_)}});
}
std::string Localization::timestamp(long long seconds) const {
    if(seconds<0||seconds>253402300799LL)return {};
    ULARGE_INTEGER ticks{};ticks.QuadPart=(static_cast<unsigned long long>(seconds)+11644473600ULL)*10000000ULL;
    FILETIME ft{ticks.LowPart,ticks.HighPart};SYSTEMTIME utc{},local{};
    if(!FileTimeToSystemTime(&ft,&utc)||!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return {};
    const std::wstring name(locale_.begin(),locale_.end());wchar_t date[128]{},time[128]{};
    if(!GetDateFormatEx(name.c_str(),DATE_SHORTDATE,&local,nullptr,date,128,nullptr)||!GetTimeFormatEx(name.c_str(),TIME_NOSECONDS,&local,nullptr,time,128))return {};
    std::wstring all=std::wstring(date)+L" "+time;int n=WideCharToMultiByte(CP_UTF8,0,all.data(),int(all.size()),nullptr,0,nullptr,nullptr);std::string result(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,all.data(),int(all.size()),result.data(),n,nullptr,nullptr);return result;
}
nlohmann::json Localization::languages() const {
    auto result=nlohmann::json::array();
    for (const auto& entry:resources()) result.push_back({{"name",entry.at("name")},{"locale",entry.at("locale")}});
    return result;
}
}
