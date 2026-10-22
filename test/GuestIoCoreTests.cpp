#include "../guest/IoManager.h"
#include <memory>
#include <iostream>
#include <cstdlib>
void Check(bool b,uint32_t n) noexcept {if(!b){std::cerr<<"I/O core failure "<<n<<'\n';std::exit(1);}}
int main(){auto m=std::make_unique<guardian::io::Manager>();auto s=std::make_unique<guardian::threads::Scheduler>();const auto n=guardian::io::IoChecks(*m,*s,Check);std::cout<<n<<" freestanding I/O checks PASS\n";}
