#include "action_sequence.h"
#include "output_codec.h"
#include "socket_output.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
using outputs::ActionSequence;
using outputs::Result;

int main() {
  // Exercise the actual sequence + wire codecs against two independent destinations.
  int sockets[2]; uint16_t ports[2];
  for (int i=0;i<2;++i) {
    sockets[i]=socket(AF_INET,SOCK_DGRAM,0);assert(sockets[i]>=0);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(sockets[i],reinterpret_cast<sockaddr*>(&local),sizeof(local))==0);
    socklen_t length=sizeof(local);assert(getsockname(sockets[i],reinterpret_cast<sockaddr*>(&local),&length)==0);
    ports[i]=ntohs(local.sin_port);
    timeval timeout{1,0};setsockopt(sockets[i],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  }
  JsonDocument document;
  auto list=document.to<JsonArray>();
  auto disabled=list.add<JsonObject>();disabled["enabled"]=false;disabled["type"]="skip";
  auto cloud=list.add<JsonObject>();cloud["enabled"]=true;cloud["type"]="cloud";
  for(int i=0;i<2;++i){auto a=list.add<JsonObject>();a["enabled"]=true;a["type"]="osc_udp";a["port"]=ports[i];a["osc"]=i?"/second":"/first";a["midi"]["value"]=42+i;}
  ActionSequence sequence;
  auto ready=[](JsonObjectConst){return true;};
  std::vector<uint16_t> destinations;
  auto submit=[&](JsonObjectConst a) {
    if(a["type"]=="cloud")return Result::Rejected;
    assert(a["type"]=="osc_udp");
    uint8_t bytes[160];size_t size=output_codec::osc(a["osc"],a["midi"]["value"],bytes,sizeof(bytes));
    destinations.push_back(a["port"]);
    auto result=socket_output::send(false,htonl(INADDR_LOOPBACK),a["port"],bytes,size,[](void*){return true;},nullptr);
    assert(result==socket_output::Result::Sent);
    return Result::Sent;
  };
  assert(sequence.start(list,100,7)==Result::Pending);
  assert(sequence.start(list,100,7)==Result::Rejected); // Busy clicks do not replace the event.
  for(int i=0;i<6;++i)sequence.poll(101+i,7,false,submit,ready);
  Result result;
  assert(sequence.takeResult(result)&&result==Result::Unknown); // Failure wasn't hidden by success.
  assert(destinations.size()==2&&destinations[0]==ports[0]&&destinations[1]==ports[1]);
  for(int i=0;i<2;++i) {
    uint8_t received[160],expected[160];
    size_t n=output_codec::osc(i?"/second":"/first",42+i,expected,sizeof(expected));
    assert(recv(sockets[i],received,sizeof(received),0)==ssize_t(n));
    assert(!memcmp(received,expected,n));close(sockets[i]);
  }
  list.remove(0);list.remove(0); // Only the two OSC actions remain.
  int calls=0;
  auto pending=[&](JsonObjectConst){++calls;return Result::Pending;};
  assert(sequence.start(list,200,7)==Result::Pending);
  sequence.poll(201,7,false,pending,ready);sequence.poll(202,7,false,pending,ready);
  assert(calls==1); // Wait for completion before the next destination.
  sequence.complete(Result::Sent);
  sequence.poll(203,7,false,pending,ready);assert(calls==2);
  sequence.complete(Result::Sent);sequence.poll(204,7,false,pending,ready);
  assert(sequence.takeResult(result)&&result==Result::Sent);
  for(int scenario=0;scenario<3;++scenario) {
    calls=0;assert(sequence.start(list,300,7)==Result::Pending);
    sequence.poll(301,7,false,pending,ready);assert(calls==1);
    sequence.poll(scenario==0?1050:302,scenario==1?8:7,scenario==2,pending,ready);
    sequence.complete(Result::Sent); // Late completion must not revive the event.
    sequence.poll(303,7,false,pending,ready);
    assert(calls==1&&!sequence.busy()&&sequence.takeResult(result)&&result==Result::Unknown);
  }
  // A held key waits for its release before another key, without losing subsequent actions.
  calls=0;assert(sequence.start(list,400,7)==Result::Pending);
  sequence.poll(401,7,false,pending,[](JsonObjectConst){return false;});assert(calls==0);
  sequence.poll(402,7,false,pending,ready);assert(calls==1);
  sequence.complete(Result::Submitted);sequence.poll(403,7,false,pending,ready);
  sequence.complete(Result::Submitted);sequence.poll(404,7,false,pending,ready);
  assert(sequence.takeResult(result)&&result==Result::Submitted);
  puts("Actions: two OSC listeners, order, disabled actions, partial failure, busy input, async completion, release waits, expiry and cancellation passed");
}
