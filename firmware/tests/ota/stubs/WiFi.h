#pragma once
constexpr int WL_CONNECTED=3;
struct Wifi { int connected=WL_CONNECTED; int status(){return connected;} };
inline Wifi WiFi;
inline void configTime(long,int,const char *,const char *) {}
