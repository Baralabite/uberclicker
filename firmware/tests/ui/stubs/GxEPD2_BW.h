#pragma once
#include "Arduino.h"
constexpr int GxEPD_WHITE=1, GxEPD_BLACK=0;
struct GxEPD2_154_D67 {
 GxEPD2_154_D67(int,int,int,int){}
 void selectSPI(SPIClass &,SPISettings){}
};
template<class Epd,int Height> struct GxEPD2_BW {
 Epd epd2;
 explicit GxEPD2_BW(Epd panel):epd2(panel){static_assert(Height==200,"panel height");}
 void init(int,bool,int,bool){}
 void setRotation(unsigned value){assert(value==0);}
 void fillScreen(int color){assert(color==GxEPD_WHITE);}
 void drawBitmap(int x,int y,const uint8_t *bytes,int width,int height,int color){
  assert(x==0&&y==0&&width==200&&height==200&&color==GxEPD_BLACK);host::bitmapFirst=bytes[0];
 }
 void display(bool partial){
  assert(!partial);host::starts.push_back(millis());host::firstPixels.push_back(host::bitmapFirst);
  host::now+=2000;if(host::onDisplay)host::onDisplay();
 }
};
