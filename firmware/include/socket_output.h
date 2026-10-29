#pragma once
#include <stdint.h>
#include <stddef.h>
namespace socket_output {
enum class Result { Rejected, Sent, Unknown };
// IPv4 is network byte order. Single nonblocking write, no cue retry.
Result send(bool tcp,uint32_t address,uint16_t port,const uint8_t *bytes,size_t size,bool (*current)(void *),void *context);
}
