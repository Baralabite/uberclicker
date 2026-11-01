#include "radio_broadcast.h"
#include <assert.h>
#include <stdio.h>
using namespace radio_broadcast;
int main(){
  Packet a;a.id=1;a.boot=7;a.sequence=1;strcpy(a.name,"Stage A");uint8_t bytes[Size];encode(a,bytes);Packet decoded;
  assert(decode(bytes,Size,decoded)&&decoded.id==1&&!strcmp(decoded.name,a.name));assert(!decode(bytes,Size-1,decoded));bytes[3]=3;assert(!decode(bytes,Size,decoded));
  Listener l;assert(!l.receive(a,-70,100));assert(l.selected==1);
  a.sequence=2;a.direction=2;assert(l.receive(a,-70,101));assert(!l.receive(a,-70,102));
  Packet b=a;b.id=2;assert(!l.receive(b,-80,103));assert(l.selected==1);assert(l.cycle(true,104)&&l.selected==2);
  a.sequence++;assert(!l.receive(a,-70,105));b.sequence++;assert(l.receive(b,-80,106));
  b.boot++;b.sequence=1;assert(!l.receive(b,-80,107));b.sequence++;assert(l.receive(b,-80,108));
  assert(!l.cycle(true,RecentMs+200));assert(l.selected==2);b.sequence++;assert(!l.receive(b,-80,RecentMs+201));
  Listener restored;restored.selected=2;
  assert(!restored.receive(a,-70,100)&&restored.selected==2);
  assert(!restored.receive(a,-70,RecentMs+200)&&restored.selected==2);
  assert(!restored.receive(b,-80,RecentMs+201)&&restored.selected==2);
  b.sequence++;assert(restored.receive(b,-80,RecentMs+202));
  Listener firstClick;assert(!firstClick.receive(a,-70,100)&&firstClick.selected==1);
  a.sequence++;assert(firstClick.receive(a,-70,101));
  puts("Broadcast: automatic selection, saved selection, expiry, reboot baseline and duplicate suppression passed");
}
