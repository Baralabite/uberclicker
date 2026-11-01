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
  host::bitmap.assign(bytes,bytes+5000);
 }
 void display(bool partial){
  assert(!partial);refresh(0,0,200,200,false);powerOff();
 }
 void powerOff(){++host::powerOffs;}
 void displayWindow(unsigned x,unsigned y,unsigned width,unsigned height){
  assert(x%8==0&&width%8==0&&width&&height&&x+width<=200&&y+height<=200);
  refresh(x,y,width,height,true);
 }
 void refresh(unsigned x,unsigned y,unsigned width,unsigned height,bool partial){
  host::windows.push_back({x,y,width,height,partial});
  for(unsigned row=y;row<y+height;++row)for(unsigned col=x/8;col<(x+width)/8;++col)
   host::screen[row*25+col]=host::bitmap[row*25+col];
  assert(host::screen==host::bitmap); // Every changed byte must be inside the window.
  host::starts.push_back(millis());host::firstPixels.push_back(host::bitmapFirst);
  host::now+=partial?500:2000;if(host::onDisplay)host::onDisplay();
 }
};
