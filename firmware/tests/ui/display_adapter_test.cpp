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
 Frame ages=c;ages.pixels[56*25+9]=0x80;ages.pixels[62*25+10]=1;
 Frame erased=c;
 Frame edge=erased;edge.pixels[Frame::Bytes-1]=1;
 Frame separated=edge;separated.pixels[12*25+20]=1;separated.pixels[181*25+1]=0x80;
 Frame filled;memset(filled.pixels,0xff,Frame::Bytes);
 Frame blank;
 Controller ui;Snapshot rx;rx.role=Role::Rx;rx.receivers[0].paired=true;rx.receivers[0].fresh=true;
 strcpy(rx.receivers[0].deviceId,"tx");strcpy(rx.receivers[0].name,"Main stage");strcpy(rx.cueReceiverId,"tx");
 rx.receivers[0].lastSeenSeconds=9;ui.setSnapshot(rx);Frame seen9;ui.render(seen9);
 rx.receivers[0].lastSeenSeconds=10;ui.setSnapshot(rx);Frame seen10;ui.render(seen10);
 std::vector<Frame> expected={b,c,ages,erased,edge,separated,filled,blank,seen9,seen10};
 host::onDisplay=[&]{
  if(host::starts.size()==1){submitDisplay(a);submitDisplay(c);}
  else if(host::starts.size()<expected.size())submitDisplay(expected[host::starts.size()]);
  else submitDisplay(expected.back()); // identical frame must not flash the panel
 };
 host::onDelay=[&]{if(host::starts.size()==1){submitDisplay(a);submitDisplay(c);}};
 try{host::task(nullptr);}catch(const host::Stop &){}
 assert(displayRefreshCount()==expected.size());
 assert(host::powerOffs==expected.size());
 for(unsigned i=0;i<expected.size();++i){
  assert(host::firstPixels[i]==expected[i].pixels[0]);
  assert(host::starts[i]==i*2500);
  assert(host::windows[i].partial==(i!=0));
 }
 const auto &single=host::windows[1];assert(single.x==0&&single.y==0&&single.width==8&&single.height==1);
 const auto &age=host::windows[2];assert(age.x==72&&age.y==56&&age.width==16&&age.height==7);
 const auto &erase=host::windows[3];assert(erase.x==72&&erase.y==56&&erase.width==16&&erase.height==7);
 const auto &last=host::windows[4];assert(last.x==192&&last.y==199&&last.width==8&&last.height==1);
 const auto &multi=host::windows[5];assert(multi.x==8&&multi.y==12&&multi.width==160&&multi.height==170);
 for(unsigned i:{6u,7u}){const auto &w=host::windows[i];assert(w.x==0&&w.y==0&&w.width==200&&w.height==200);}
 const auto &seen=host::windows.back();assert(seen.x>=12&&seen.y>=180&&seen.y+seen.height<=187&&seen.width<=176);
 puts("Display adapter tests passed: allocation failures, nonblocking submit, coalescing, identical skip, 2.5 s cadence, diff bounds, erasure, edges and RX age rollover");
}
