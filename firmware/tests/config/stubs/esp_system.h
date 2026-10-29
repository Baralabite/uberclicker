#pragma once
#include <cstdint>
inline uint32_t esp_random(){static uint32_t x=19583717;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
