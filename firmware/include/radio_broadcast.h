#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace radio_broadcast {
constexpr size_t Size = 48;
constexpr uint32_t RecentMs = 45000;
struct Packet { uint64_t id=0; uint32_t boot=0, sequence=0; uint8_t direction=0; char name[25]={}; };
inline void encode(const Packet &p, uint8_t *b) {
  memset(b,0,Size); b[0]='U'; b[1]='B'; b[2]=1; b[3]=p.direction;
  for(unsigned i=0;i<6;++i)b[4+i]=p.id>>(i*8);
  for(unsigned i=0;i<4;++i){b[10+i]=p.boot>>(i*8);b[14+i]=p.sequence>>(i*8);}
  memcpy(b+18,p.name,24);
}
inline bool decode(const uint8_t *b,size_t n,Packet &p) {
  if(n!=Size || b[0]!='U'||b[1]!='B'||b[2]!=1||b[3]>2)return false;
  p=Packet{};p.direction=b[3];
  for(unsigned i=0;i<6;++i)p.id|=uint64_t(b[4+i])<<(i*8);
  for(unsigned i=0;i<4;++i){p.boot|=uint32_t(b[10+i])<<(i*8);p.sequence|=uint32_t(b[14+i])<<(i*8);}
  memcpy(p.name,b+18,24);return p.id && p.boot && p.sequence;
}
struct Seen { Packet packet; uint32_t at=0; int16_t rssi=0; bool used=false; };
struct Listener {
  Seen seen[8]; uint64_t selected=0;
  bool recent(const Seen &s,uint32_t now) const {return s.used && uint32_t(now-s.at)<RecentMs;}
  // First packet establishes a baseline; never replay a cue on discovery/reboot.
  bool receive(const Packet &p,int16_t rssi,uint32_t now) {
    Seen *slot=nullptr;
    for(auto &s:seen)if(s.used&&s.packet.id==p.id){slot=&s;break;}
    if(!slot)for(auto &s:seen)if(!s.used||(!recent(s,now)&&s.packet.id!=selected)){slot=&s;break;}
    if(!slot)return false;
    bool established=slot->used&&recent(*slot,now)&&slot->packet.boot==p.boot;
    if(established&&p.sequence<=slot->packet.sequence)return false;
    *slot={p,now,rssi,true};
    if(!selected)selected=p.id;
    return established&&p.id==selected&&p.direction!=0;
  }
  bool cycle(bool forward,uint32_t now) {
    unsigned indices[8],n=0,current=0;bool found=false;
    for(unsigned i=0;i<8;++i)if(recent(seen[i],now)){indices[n]=i;if(seen[i].packet.id==selected){current=n;found=true;}++n;}
    if(!n)return false;
    unsigned next=found?(current+(forward?1:n-1))%n:(forward?0:n-1);
    selected=seen[indices[next]].packet.id;return true;
  }
};
}
