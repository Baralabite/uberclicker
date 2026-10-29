#include "radio_protocol.h"
#include "output_codec.h"
#include "output_safety.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace radio_protocol;
static uint8_t entropy=1;
static void randomBytes(void *p,size_t n){for(size_t i=0;i<n;++i)static_cast<uint8_t*>(p)[i]=entropy++;}
static uint8_t key[32];
static void connect(Session &tx,Session &rx,Packet &hello,Packet &challenge){
 Packet reply;bool answer=false;tx.hello(hello);assert(!rx.receive(hello,challenge,answer)&&answer);
 assert(!tx.receive(challenge,reply,answer)&&!answer&&tx.ready());
}
int main(){
 memset(key,0x42,32);
 assert(wireIdentity(key,1)==0x923c2a1a4c5dfc5bULL); // independent Python hmac fixture
 uint8_t otherKey[32]={1};assert(wireIdentity(key,1)&&wireIdentity(key,1)!=wireIdentity(key,2));
 assert(wireIdentity(key,1)!=wireIdentity(otherKey,1));assert(wireIdentity(key,1)==wireIdentity(key,1));
 Session tx,rx;Packet hello,challenge,cue,reply;bool answer=false;
 tx.begin(true,key,1,2,randomBytes);rx.begin(false,key,2,1,randomBytes);
 assert(!tx.cue(1,cue));connect(tx,rx,hello,challenge);assert(tx.cue(1,cue));
 Packet forged;memcpy(forged,cue,Size);forged[60]=2;assert(!rx.receive(forged,reply,answer));
 assert(rx.receive(cue,reply,answer));assert(!answer);
 rx.acknowledge(cue,Submitted,reply);assert(tx.ackMatches(reply,cue));
 Packet ack;memcpy(ack,reply,Size);ack[60]=Received;assert(!tx.ackMatches(ack,cue));
 memcpy(ack,reply,Size);write64(ack+4,3);sign(ack,key);assert(!tx.ackMatches(ack,cue));
 memcpy(ack,reply,Size);write64(ack+52,2);sign(ack,key);assert(!tx.ackMatches(ack,cue));
 assert(!rx.receive(cue,reply,answer)&&answer&&reply[60]==Submitted); // exact duplicate acknowledges only
 Packet first;memcpy(first,cue,Size);assert(tx.cue(2,cue));assert(rx.receive(cue,reply,answer));
 assert(!rx.receive(first,reply,answer)&&!answer); // stale sequence
 Session stranger;stranger.begin(true,key,3,2,randomBytes);stranger.hello(forged);assert(!rx.receive(forged,reply,answer));
 uint8_t wrong[32]={1};stranger.begin(true,wrong,1,2,randomBytes);stranger.hello(forged);assert(!rx.receive(forged,reply,answer));
 // A late result for an older network cue must not corrupt the newest duplicate's result.
 Packet inFlight,newer;memcpy(inFlight,cue,Size);
 assert(tx.cue(1,newer));assert(rx.receive(newer,reply,answer));rx.acknowledge(newer,Rejected,reply);
 rx.acknowledge(inFlight,Sent,reply);assert(tx.ackMatches(reply,inFlight));
 assert(!rx.receive(newer,reply,answer)&&answer&&reply[60]==Rejected);
 // Receiver reboot rejects every old cue, even after replaying the recorded hello.
 rx.begin(false,key,2,1,randomBytes);assert(!rx.receive(first,reply,answer));
 assert(!rx.receive(hello,reply,answer)&&answer);assert(!rx.receive(first,reply,answer));
 // Sender reboot cannot use a recorded receiver challenge.
 tx.begin(true,key,1,2,randomBytes);assert(!tx.receive(challenge,reply,answer));assert(!tx.ready());
 connect(tx,rx,hello,challenge);assert(tx.cue(1,cue));assert(rx.receive(cue,reply,answer));
 // Pause/resume rotates the sender's session, allowing sequence restart safely.
 tx.invalidate();assert(!tx.cue(1,cue));connect(tx,rx,hello,challenge);assert(tx.cue(2,cue));assert(rx.receive(cue,reply,answer));
 Packet old;memcpy(old,cue,Size);rx.invalidate();assert(!rx.receive(old,reply,answer));
 assert(!rx.receive(hello,reply,answer)&&answer);assert(!rx.receive(old,reply,answer));
 // Clock transfer rejects first deliveries held by an attacker, including wrap.
 tx.begin(true,key,1,2,randomBytes);rx.begin(false,key,2,1,randomBytes);
 tx.hello(hello);assert(!rx.receive(hello,challenge,answer,0xfffffff0,144)&&answer);
 assert(!tx.receive(challenge,reply,answer,500,144));
 assert(tx.cue(1,cue,600));
 assert(!rx.receive(cue,reply,answer,2000,144));
 assert(rx.receive(cue,reply,answer,372,144));
 Session unpaired;uint8_t zero[32]={};unpaired.begin(false,zero,2,1,randomBytes);assert(!unpaired.receive(hello,reply,answer));
 uint8_t bytes[160];const uint8_t expected[]={ '/', 'n','e','x','t',0,0,0,',','i',0,0,0,0,0,100 };
 assert(output_codec::osc("/next",100,bytes,sizeof(bytes))==16&&!memcmp(bytes,expected,16));
 assert(output_codec::osc("/",-1,bytes,sizeof(bytes))==12&&bytes[8]==255&&bytes[11]==255);
 assert(!output_codec::osc("bad",0,bytes,sizeof(bytes)));assert(!output_codec::osc("/with space",0,bytes,sizeof(bytes)));
 assert(output_codec::raw("next",false,bytes,sizeof(bytes))==4&&!memcmp(bytes,"next",4));
 assert(output_codec::raw("next",true,bytes,sizeof(bytes))==5&&!memcmp(bytes,"next\n",5));assert(!output_codec::raw("x\ny",true,bytes,sizeof(bytes)));
 char large[130];memset(large,'x',129);large[129]=0;assert(!output_codec::raw(large,false,bytes,sizeof(bytes)));
 assert(output_codec::midi(0,16,127,100,false,bytes)==3&&bytes[0]==0x9f&&bytes[1]==127&&bytes[2]==100);
 assert(output_codec::midi(0,16,127,100,true,bytes)==3&&bytes[0]==0x8f&&bytes[2]==0);
 assert(output_codec::midi(1,1,64,127,false,bytes)==3&&bytes[0]==0xb0);
 assert(output_codec::midi(2,3,12,127,false,bytes)==2&&bytes[0]==0xc2&&bytes[1]==12);
 assert(!output_codec::midi(0,0,60,100,false,bytes));assert(!output_codec::midi(0,17,60,100,false,bytes));
 assert(!output_codec::midi(0,1,128,100,false,bytes));assert(!output_codec::midi(1,1,60,100,true,bytes));
 output_safety::Release release;
 assert(!release.canKey()&&!release.canMidi());
 release.keyboard=false;release.midi=false;assert(release.canKey()&&release.canMidi());
 release.note=true;assert(!release.canMidi());release.reconnect();assert(!release.canKey()&&!release.canMidi()&&!release.note);
 release.keyboard=false;release.midi=false;release.pause();assert(!release.canKey()&&!release.canMidi());
 assert(output_safety::current(false,1,1,0xfffffff0,20));
 assert(!output_safety::current(false,1,1,0xfffffff0,1000));
 assert(!output_safety::current(true,1,1,0,1));assert(!output_safety::current(false,1,2,0,1));
 puts("radio authentication/reboot/replay + output codec checks passed");
}
