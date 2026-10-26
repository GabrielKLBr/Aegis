#include "Desktop.h"
namespace guardian::desktop {
namespace {
#include "DesktopFont.inl"
Rect Intersection(Rect a,Rect b) noexcept {Rect r{a.left>b.left?a.left:b.left,a.top>b.top?a.top:b.top,a.right<b.right?a.right:b.right,a.bottom<b.bottom?a.bottom:b.bottom};return r.right<=r.left||r.bottom<=r.top?Rect{}:r;}
struct Raster {
    uint32_t* pixels;Rect clip;uint32_t stride=Width;int32_t originX=0,originY=0;
    void Box(Rect rect,uint32_t color) const noexcept {rect=Intersection(rect,clip);for(int32_t y=rect.top;y<rect.bottom;++y)for(int32_t x=rect.left;x<rect.right;++x)pixels[uint32_t(y-originY)*stride+uint32_t(x-originX)]=0xFF000000u|(color&0xFFFFFF);}
    void Pixel(int32_t x,int32_t y,uint32_t color) const noexcept {if(x>=clip.left&&x<clip.right&&y>=clip.top&&y<clip.bottom)pixels[uint32_t(y-originY)*stride+uint32_t(x-originX)]=0xFF000000u|(color&0xFFFFFF);}
    void Glyphs(int32_t x,int32_t y,const uint16_t* text,uint32_t count,uint32_t color,uint32_t width,uint32_t height,bool bold=false,bool italic=false,bool underline=false) const noexcept {
        constexpr uint8_t lower[26][5]={{32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{24,164,164,164,124},{127,8,4,4,120},{0,68,125,64,0},{64,128,132,125,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,120,4,120},{124,8,4,4,120},{56,68,68,68,56},{252,36,36,36,24},{24,36,36,40,252},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{28,160,160,160,124},{68,100,84,76,68}};
        for(uint32_t i=0;i<count;++i){const auto c=text[i];if(c==' ')continue;const uint8_t* glyph=nullptr;uint8_t other[5]{127,65,65,65,127};
            if(c>='0'&&c<='9')glyph=font[c-'0'];else if(c>='A'&&c<='Z')glyph=font[c-'A'+10];else if(c>='a'&&c<='z')glyph=lower[c-'a'];else {
                if(c=='.'){other[0]=other[1]=other[3]=other[4]=0;other[2]=64;}else if(c==':'){other[0]=other[1]=other[3]=other[4]=0;other[2]=36;}else if(c=='-'){for(auto& v:other)v=8;}else if(c=='!'){other[0]=other[1]=other[3]=other[4]=0;other[2]=95;}else if(c=='/'){other[0]=64;other[1]=32;other[2]=16;other[3]=8;other[4]=4;}else if(c=='?')glyph=font[36];glyph=glyph?glyph:other;
            }
            for(uint32_t col=0;col<5;++col)for(uint32_t row=0;row<7;++row)if(glyph[col]&(1u<<row)){const auto left=x+int32_t(i*width+col*width/6),top=y+int32_t(row*height/8);const auto right=x+int32_t(i*width+(col+1)*width/6)+(bold?1:0),bottom=y+int32_t((row+1)*height/8);const auto slant=italic?int32_t((6-row)*width/20):0;Box({left+slant,top,right+slant,bottom},color);}
        }if(underline)Box({x,y+int32_t(height)-2,x+int32_t(count*width),y+int32_t(height)-1},color);
    }
};
}
namespace {
void RenderState(const State& state,uint32_t* pixels,const Surface* selected) noexcept {
    const Rect screen=selected?selected->info.client:Rect{0,0,int32_t(Width),int32_t(Height)};const uint32_t stride=selected?uint32_t(screen.right-screen.left):Width;const int32_t originX=selected?screen.left:0,originY=selected?screen.top:0;Raster whole{pixels,screen,stride,originX,originY};whole.Box(screen,0x172536);
    uint32_t order[MaxWindows]{},count=0;uint64_t keys[MaxWindows]{};for(uint32_t i=0;i<MaxWindows;++i){const auto& w=state.surfaces[i];if(!w.info.handle||!(w.info.flags&1))continue;const Surface* root=&w;uint32_t depth=0;bool visible=true;for(uint32_t k=0;k<MaxWindows&&root->info.parent;++k){const Surface* parent=nullptr;for(const auto& p:state.surfaces)if(p.info.handle==root->info.parent)parent=&p;if(!parent){visible=false;break;}if(!(parent->info.flags&1))visible=false;root=parent;++depth;}if(!visible||(selected&&root!=selected))continue;const auto key=(root->info.exStyle&8?1ull<<48:0ull)+root->z*64+depth;uint32_t at=count;while(at&&keys[at-1]>key){keys[at]=keys[at-1];order[at]=order[at-1];--at;}keys[at]=key;order[at]=i;++count;}
    for(uint32_t at=0;at<count;++at){const auto& s=state.surfaces[order[at]];const auto& w=s.info;auto bounds=screen;const Surface* current=&s;for(uint32_t k=0;k<MaxWindows&&current->info.parent;++k){const Surface* parent=nullptr;for(const auto& p:state.surfaces)if(p.info.handle==current->info.parent)parent=&p;if(!parent)break;bounds=Intersection(bounds,parent->info.client);current=parent;}
        Raster outside{pixels,Intersection(bounds,w.bounds),stride,originX,originY};if(&s!=selected)outside.Box(w.bounds,w.flags&4?0x292929:0xB8BDC6);
        if(&s!=selected&&(w.style&0xC00000)==0xC00000){Rect caption{w.bounds.left+1,w.bounds.top+1,w.bounds.right-1,w.client.top};outside.Box(caption,state.focus==w.handle?(w.flags&4?0x202020:0x1559A0):(w.flags&4?0x343434:0x727B88));uint32_t n=0;while(n<64&&w.title[n])++n;outside.Glyphs(w.bounds.left+6,w.bounds.top+6,w.title,n,0xFFFFFF,7,14);}
        Raster client{pixels,Intersection(bounds,w.client),stride,originX,originY};client.Box(w.client,s.background);
        for(uint32_t i=0;i<s.drawings;++i){const auto& p=s.commands[i];auto clip=p.clip;clip.left+=w.client.left;clip.right+=w.client.left;clip.top+=w.client.top;clip.bottom+=w.client.top;Raster r{pixels,Intersection(client.clip,clip),stride,originX,originY};auto rect=p.rect;rect.left+=w.client.left;rect.right+=w.client.left;rect.top+=w.client.top;rect.bottom+=w.client.top;
            if(p.kind==Draw::Fill)r.Box(rect,p.color);
            else if(p.kind==Draw::Text){if((p.mode&0xFF)==2)r.Box({rect.left,rect.top,rect.left+int32_t(p.count*p.width),rect.top+int32_t(p.height)},p.background);r.Glyphs(rect.left,rect.top,p.text,p.count,p.color,p.width,p.height,(p.mode&0x100)!=0,(p.mode&0x200)!=0,(p.mode&0x400)!=0);}
            else if(p.kind==Draw::Line){int32_t x=rect.left,y=rect.top;const int32_t dx=rect.right>x?rect.right-x:x-rect.right,dy=rect.bottom>y?y-rect.bottom:rect.bottom-y,sx=x<rect.right?1:-1,sy=y<rect.bottom?1:-1;int32_t error=dx+dy;for(uint32_t n=0;n<65537;++n){if(x==rect.right&&y==rect.bottom)break;r.Pixel(x,y,p.color);const auto e=2*error;if(e>=dy){error+=dy;x+=sx;}if(e<=dx){error+=dx;y+=sy;}}}
            else if(p.kind==Draw::Pixels){const auto width=uint32_t(rect.right-rect.left);if(!width)continue;for(uint32_t n=0;n<p.count;++n){const auto color=uint32_t(p.text[n*2])|(uint32_t(p.text[n*2+1])<<16);if(color>>24)r.Pixel(rect.left+int32_t(n%width),rect.top+int32_t(n/width),color);}}
        }
    }
}
}
void Core::Render(uint32_t* pixels) const noexcept {RenderState(*state_,pixels,nullptr);}
bool Core::RenderWindow(uint64_t handle,uint32_t* pixels,uint32_t capacity,uint32_t& width,uint32_t& height) const noexcept {
    if(!state_||!pixels)return false;const Surface* selected=nullptr;for(const auto& s:state_->surfaces)if(s.info.handle==handle)selected=&s;
    if(!selected||selected->info.parent||!(selected->info.flags&1))return false;
    const auto& client=selected->info.client;const int64_t w=int64_t(client.right)-client.left,h=int64_t(client.bottom)-client.top;
    if(w<=0||h<=0||w>presentation::MaxWidth||h>presentation::MaxHeight||uint64_t(w)*uint64_t(h)>capacity)return false;
    width=uint32_t(w);height=uint32_t(h);RenderState(*state_,pixels,selected);return true;
}

bool ValidFrame(const Frame& f) noexcept {if(f.width!=Width||f.height!=Height||f.stride!=Width*4||f.bytes!=PixelCount*4||f.count>MaxWindows||f.reserved)return false;bool focus=!f.focus;
    for(uint32_t i=0;i<f.count;++i){const auto& w=f.windows[i];if((w.parent&&w.owner)||!w.handle||!w.pid||!w.tid||w.dpi!=96||w.flags&~63u||w.bounds.left< -32768||w.bounds.top< -32768||w.bounds.right>32768||w.bounds.bottom>32768||w.bounds.right<=w.bounds.left||w.bounds.bottom<=w.bounds.top||w.bounds.right-w.bounds.left>4096||w.bounds.bottom-w.bounds.top>4096||w.client.left<w.bounds.left||w.client.top<w.bounds.top||w.client.right>w.bounds.right||w.client.bottom>w.bounds.bottom||w.client.right<w.client.left||w.client.bottom<w.client.top)return false;
        bool ended=false,parent=!w.parent,owner=!w.owner;for(auto c:w.title){if(!c)ended=true;else if(ended||c<32)return false;}if(!ended)return false;
        for(uint32_t j=0;j<f.count;++j){if(i!=j&&f.windows[j].handle==w.handle)return false;if(f.windows[j].handle==w.parent&&i!=j&&f.windows[j].pid==w.pid)parent=true;if(f.windows[j].handle==w.owner&&i!=j&&f.windows[j].pid==w.pid)owner=true;}if(!parent||!owner)return false;if(f.focus==w.handle)focus=true;
        uint64_t next=w.parent?w.parent:w.owner;for(uint32_t depth=0;next;++depth){if(depth>=f.count||next==w.handle)return false;bool found=false;for(uint32_t j=0;j<f.count;++j)if(f.windows[j].handle==next){next=f.windows[j].parent?f.windows[j].parent:f.windows[j].owner;found=true;break;}if(!found)return false;}
    }return focus;
}
}
