#include "device_ui.h"
#include "Arduino.h"
#include <stdio.h>
using namespace device_ui;
int main(){
 Frame a,b,c; a.pixels[0]=1;b.pixels[0]=2;c.pixels[0]=3;
 submitDisplay(a);assert(displayRefreshCount()==0);
 host::failQueue=true;assert(!beginDisplay());host::failQueue=false;
 host::failTask=true;assert(!beginDisplay());assert(host::deletions==1);host::failTask=false;
 assert(beginDisplay());assert(beginDisplay());assert(host::allocations==2);
 submitDisplay(a);submitDisplay(b); // producer returns before any panel work
 assert(host::starts.empty());
 host::onDisplay=[&]{
  if(host::starts.size()==1){submitDisplay(a);submitDisplay(c);}
  else submitDisplay(c); // identical frame must not flash the panel
 };
 host::onDelay=[&]{submitDisplay(a);submitDisplay(c);};
 try{host::task(nullptr);}catch(const host::Stop &){}
 assert(displayRefreshCount()==2);
 assert((host::firstPixels==std::vector<uint8_t>{2,3}));
 assert((host::starts==std::vector<uint32_t>{0,2500}));
 puts("Display adapter tests passed: allocation failures, nonblocking submit, latest-frame coalescing, identical skip, 2.5 s cadence");
}
