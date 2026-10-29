#pragma once
#include <stdint.h>
namespace output_safety {
// Unsigned subtraction remains correct through millis() rollover.
inline bool current(bool paused,uint32_t issuedGeneration,uint32_t generation,uint32_t issuedAt,uint32_t now) {
 return !paused&&issuedGeneration==generation&&uint32_t(now-issuedAt)<750;
}
// A release must succeed before another press. On reconnect, only releases run.
struct Release {
 bool keyboard=true, midi=true, note=false;
 uint8_t channel=1, number=0;
 void reconnect(){keyboard=true;midi=true;note=false;}
 void pause(){keyboard=true;midi=true;}
 bool canKey()const{return !keyboard;}
 bool canMidi()const{return !midi&&!note;}
};
}
