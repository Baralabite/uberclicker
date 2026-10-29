#pragma once
#include <stdint.h>
#include <stddef.h>
namespace output_codec {
// One OSC int32 argument, big endian, padded strings. Returns 0 on invalid input.
size_t osc(const char *address, int32_t value, uint8_t *out, size_t capacity);
// Raw payload UTF-8; TCP appends exactly one LF and rejects embedded CR/LF.
size_t raw(const char *payload, bool tcp, uint8_t *out, size_t capacity);
// MIDI channel is 1..16. Program number is the program, value is ignored.
size_t midi(uint8_t type, uint8_t channel, uint8_t number, uint8_t value, bool release, uint8_t *out);
}
