#include "NativeGuiManifest.h"
#include <set>
#include <stdexcept>
#include <vector>
namespace av::microvm::native_gui {
namespace {
class Parser {
    std::string_view text;size_t at=0,nodes=0;Manifest fields;std::vector<std::string> path;
    void Need(bool value){if(!value)throw std::runtime_error("Native GUI manifest JSON rejected");}
    void Space(){while(at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))++at;}
    bool Take(char c){Space();if(at<text.size()&&text[at]==c){++at;return true;}return false;}
    bool Digit(){return at<text.size()&&text[at]>='0'&&text[at]<='9';}
    std::string String(){Need(Take('"'));std::string out;bool end=false;while(at<text.size()){auto c=text[at++];if(c=='"'){end=true;break;}Need(uint8_t(c)>=32&&uint8_t(c)<127);if(c=='\\'){Need(at<text.size());c=text[at++];if(c=='u'){uint32_t code=0;for(unsigned i=0;i<4;++i){Need(at<text.size());const auto h=text[at++];Need(h>='0'&&h<='9'||h>='a'&&h<='f'||h>='A'&&h<='F');code=code*16+uint32_t(h<='9'?h-'0':h<='F'?h-'A'+10:h-'a'+10);}Need(code!=0);if(code>127){out+="\\u";out+=std::to_string(code);continue;}c=char(code);}else if(c=='b')c='\b';else if(c=='f')c='\f';else if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else Need(c=='"'||c=='\\'||c=='/');}out+=c;Need(out.size()<=65536);}Need(end);return out;}
    void Field(const std::string& value,bool quoted){if(path.size()==1&&path[0]=="phase"){Need(quoted);fields.phase=value;}if(path.size()==2&&path[0]=="nativeGuiRuntime"){if(path[1]=="backend"){Need(quoted);fields.backend=value;}if(path[1]=="helperSha256"){Need(quoted);fields.helperSha256=value;}if(path[1]=="protocol"){Need(!quoted);fields.protocol=value;}}}
    void Value(unsigned depth){Need(depth<=32&&++nodes<=16384);Space();Need(at<text.size());if(text[at]=='{'){++at;std::set<std::string> keys;if(Take('}'))return;do{auto key=String();Need(keys.insert(key).second&&Take(':'));path.push_back(std::move(key));Value(depth+1);path.pop_back();if(Take('}'))return;Need(Take(','));}while(true);}if(text[at]=='['){++at;if(Take(']'))return;do{path.push_back("[]");Value(depth+1);path.pop_back();if(Take(']'))return;Need(Take(','));}while(true);}if(text[at]=='"'){Field(String(),true);return;}const auto start=at;for(const auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(text.substr(at,literal.size())==literal){at+=literal.size();Field(std::string(literal),false);return;}if(text[at]=='-')++at;Need(Digit());if(text[at]=='0')++at;else while(Digit())++at;if(at<text.size()&&text[at]=='.'){++at;Need(Digit());while(Digit())++at;}if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;Need(Digit());while(Digit())++at;}Field(std::string(text.substr(start,at-start)),false);}
public:explicit Parser(std::string_view input):text(input){}Manifest Read(){Need(text.size()<=(4u<<20));Space();Need(at<text.size()&&text[at]=='{');Value(0);Space();Need(at==text.size()&&!fields.phase.empty());return fields;}
};
}
Manifest ReadManifest(std::string_view text){return Parser(text).Read();}
}
