#include "socket_output.h"
#ifdef ARDUINO
#include <lwip/sockets.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <errno.h>
namespace socket_output {
Result send(bool tcp,uint32_t address,uint16_t port,const uint8_t *bytes,size_t size,bool (*current)(void *),void *context) {
 if(!size||size>160||!port||!current(context))return Result::Rejected;
 int fd=socket(AF_INET,tcp?SOCK_STREAM:SOCK_DGRAM,0);if(fd<0)return Result::Rejected;
 if(fcntl(fd,F_SETFL,O_NONBLOCK)<0){close(fd);return Result::Rejected;}
#ifndef ARDUINO
#ifdef SO_NOSIGPIPE
 int yes=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
#endif
 sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(port);endpoint.sin_addr.s_addr=address;
 bool opened=connect(fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint))==0;
 if(!opened&&errno==EINPROGRESS){
  fd_set writable;FD_ZERO(&writable);FD_SET(fd,&writable);timeval timeout{0,300000};
  if(select(fd+1,nullptr,&writable,nullptr,&timeout)>0){int error=0;socklen_t length=sizeof(error);opened=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)==0&&error==0;}
 }
 Result result=Result::Rejected;
 if(opened&&current(context)) {
#ifdef MSG_NOSIGNAL
  constexpr int flags=MSG_DONTWAIT|MSG_NOSIGNAL;
#else
  constexpr int flags=MSG_DONTWAIT;
#endif
  result=::send(fd,bytes,size,flags)==ssize_t(size)?Result::Sent:Result::Unknown;
 }
 close(fd);return result;
}
}
