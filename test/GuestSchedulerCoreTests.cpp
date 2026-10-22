#include "../guest/Scheduler.h"
#include <cstdlib>
#include <iostream>
void Check(bool ok,uint32_t n) noexcept {if(!ok){std::cerr<<"Scheduler core failure "<<n<<'\n';std::exit(1);}}
int main(){const auto count=guardian::threads::SchedulerChecks(Check);Check(count>400,0);std::cout<<count<<" freestanding scheduler checks PASS\n";return 0;}
