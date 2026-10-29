#pragma once
#include <stdint.h>
namespace guardian {
// One complete UTF-8 write, excluding terminal controls and overlong encodings.
struct ConsoleUtf8Decoder {
    uint32_t remaining=0,point=0,minimum=0;bool failed=false;
    bool Put(uint8_t byte) noexcept {
        if(failed)return false;
        if(remaining){if((byte&0xC0)!=0x80){failed=true;return false;}point=(point<<6)|(byte&63);if(--remaining)return true;}
        else if(byte>=0xC2&&byte<=0xDF){point=byte&31;remaining=1;minimum=0x80;return true;}
        else if(byte>=0xE0&&byte<=0xEF){point=byte&15;remaining=2;minimum=0x800;return true;}
        else if(byte>=0xF0&&byte<=0xF4){point=byte&7;remaining=3;minimum=0x10000;return true;}
        else {if(byte>=0x80){failed=true;return false;}point=byte;minimum=0;}
        failed=!(point>=minimum&&point<=0x10FFFF&&!(point>=0xD800&&point<=0xDFFF)&&!(point<32&&point!=9&&point!=10&&point!=13)&&!(point>=127&&point<160));return !failed;
    }
};
inline bool ConsoleUtf8(const uint8_t* bytes,uint32_t count) noexcept {
    ConsoleUtf8Decoder decoder;for(uint32_t i=0;i<count;++i)if(!decoder.Put(bytes[i]))return false;return !decoder.remaining;
}
}
