// Ordinary MSVC /MD application: no custom entry point or CRT replacement.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <clocale>
#include <cerrno>
#include <iostream>
static int initialized=0,unwound=0;
struct Global {Global(){initialized=17;}~Global(){std::puts("CRT global destructor");}} global;
__declspec(thread) int localValue=41;
static volatile long tlsDestructors=0;
struct Local {int value;Local():value(41){}~Local(){InterlockedIncrement(&tlsDestructors);}};
static thread_local Local dynamicLocal;
static void AtExit(){std::puts("CRT atexit");}
struct Cleanup {~Cleanup(){++unwound;}};
__declspec(noinline) static void Throw(){Cleanup cleanup;throw 73;}
static std::mutex gate;
static std::condition_variable changed;
static bool ready=false;
static int workerResult=0;
static void Worker(){
    if(localValue!=41||dynamicLocal.value!=41)return;
    dynamicLocal.value=99;localValue=99;errno=77;
    {std::lock_guard<std::mutex> lock(gate);ready=true;workerResult=37;}
    changed.notify_one();
}
int main(int argc,char** argv){
    if(initialized!=17||localValue!=41||argc<1||!argv||!argv[0]||!std::getenv("PATH"))return 1;
    if(argc!=1&&(argc!=3||std::strcmp(argv[1],"hello world")||std::strcmp(argv[2],"--child")))return 15;
    if(std::atexit(AtExit))return 2;
    std::vector<int> values{1,2,3,4};values.push_back(5);
    int sum=0;for(auto x:values)sum+=x;
    const std::string message=std::string("original")+" C++";
    if(sum!=15||message!="original C++"||std::sqrt(double(sum+1))!=4.)return 3;
    try {Throw();return 4;}
    catch(int code){if(code!=73||unwound!=1)return 5;}
    if(message!="original C++"||values.size()!=5||values[4]!=5)return 6;
    try {Cleanup cleanup;throw std::runtime_error("guest exception");}
    catch(const std::runtime_error& e){if(std::strcmp(e.what(),"guest exception")||unwound!=2)return 7;}
    errno=33;
    std::thread worker(Worker);
    {std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[]{return ready;});}
    worker.join();
    if(workerResult!=37||errno!=33||localValue!=41||tlsDestructors!=1)return 8;
    auto* file=std::fopen("crt-data.txt","w+b");if(!file)return 9;
    const char data[]="CRT guest file";
    if(std::fwrite(data,1,sizeof(data),file)!=sizeof(data)||std::fflush(file)||std::fseek(file,0,SEEK_SET))return 10;
    char read[sizeof(data)]{};
    if(std::fread(read,1,sizeof(read),file)!=sizeof(read)||std::memcmp(read,data,sizeof(data))||std::fclose(file)||std::remove("crt-data.txt"))return 11;
    if(!std::setlocale(LC_CTYPE,".UTF8"))return 12;
    wchar_t wide[8]{};
    if(std::mbstowcs(wide,"A\xC3\xA7\xF0\x9F\x98\x80",8)!=4||wide[1]!=0xE7||wide[2]!=0xD83D||wide[3]!=0xDE00)return 13;
    std::cout.write("CRT MSVCP iostream\n",19).flush();if(!std::cout)return 14;
    auto* block=static_cast<unsigned char*>(std::malloc(64));if(!block)return 16;
    std::memset(block,0x5A,64);
    block=static_cast<unsigned char*>(std::realloc(block,256));if(!block)return 17;
    for(unsigned i=0;i<64;++i)if(block[i]!=0x5A)return 18;
    std::free(block);
    double (*volatile squareRoot)(double)=std::sqrt;errno=0;
    if(!std::isnan(squareRoot(-1.))||errno!=EDOM)return 19;
    if(std::fopen("crt-missing.txt","rb")||errno!=ENOENT)return 20;
    if(std::printf("CRT 14.5 main argc=%d sum=%d %s\n",argc,sum,message.c_str())<0||std::fflush(stdout))return 21;
    std::puts("CRT exceptions TLS files locale math PASS");
    return 0;
}
