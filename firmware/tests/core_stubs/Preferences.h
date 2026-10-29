#pragma once
#include <stddef.h>
inline bool stored=false,present=false,openFailure=false,writeFailure=false;
class Preferences {
 public:
 bool begin(const char *,bool){return !openFailure;}
 void end(){}
 size_t putBool(const char *,bool value){if(writeFailure)return 0;stored=value;present=true;return sizeof(bool);}
 bool getBool(const char *,bool fallback){return present?stored:fallback;}
};
