#include "ota_update.h"
#include "ota_manifest.h"
#include "config_service.h"
#include "Arduino.h"
#include "WiFi.h"
#include "esp_ota_ops.h"
#include <openssl/sha.h>
#include <fstream>
#include <iterator>
using namespace ota_update;
static std::string validFeed, validImage;
static void workerUntilResult() {
  host::idle=[] { poll();throw host::Stop{}; };
  try { host::task(nullptr); } catch(const host::Stop &) {}
}
static void reset() {
  close(); assert(!enabled());
  host::feed=validFeed;host::image=validImage;host::truncated=-1;host::response=200;host::badImage=false;
  host::begins=host::aborts=host::ends=host::boots=host::requests=0;WiFi.connected=WL_CONNECTED;
  open();assert(enabled() && !busy());
}
static void attemptInstall() {
  check();assert(busy());close();assert(enabled());
  host::idle=[] {
    poll();assert(status().state==State::Available);install();assert(busy());
    host::idle=[] { poll();throw host::Stop{}; };
  };
  try { host::task(nullptr); } catch(const host::Stop &) {}
}
int main(int argc,char **argv) {
  assert(argc==2);
  std::ifstream file(argv[1]);validFeed=std::string(std::istreambuf_iterator<char>(file),{});
  validImage=std::string(8192,'\0');memcpy(validImage.data()+48,"0.2.0-test.1",12);memcpy(validImage.data()+80,"UberClicker",11);
  unsigned char digest[32];SHA256((const unsigned char *)validImage.data(),validImage.size(),digest);
  char hex[65]={};for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
  JsonDocument manifest;assert(!deserializeJson(manifest,validFeed));manifest["images"][0]["sha256"]=hex;
  validFeed.clear();serializeJson(manifest,validFeed);
  Candidate candidate;assert(parseManifest(manifest.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));
  assert(validVersion("0.2.0-test.1") && !validVersion("../../other") && !validVersion("0.2.0-"));
  for(auto value:{"../bad","0.2.0-","0.2.0-a/b","0.2.0-a?b","0.2.0-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})assert(!validVersion(value));
  for(auto key:{"board","chip","channel"}){JsonDocument bad=manifest;bad[key]="other";assert(!parseManifest(bad.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));}
  {JsonDocument bad=manifest;bad["partitions"][2]["size"]=0x300000;assert(!parseManifest(bad.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));}
  {JsonDocument bad=manifest;bad["images"][0]["size"]=SlotSize+1;assert(!parseManifest(bad.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));}
  {JsonDocument bad=manifest;bad["images"][0]["sha256"]="bad";assert(!parseManifest(bad.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));}
  {JsonDocument bad=manifest;bad["settingsSchemaVersion"]=99;assert(!parseManifest(bad.as<JsonObjectConst>(),candidate,uc::kConfigSchemaVersion,uc::kConfigProtocolVersion));}
  check();install();assert(!enabled() && host::requests==0); // No networking outside the physical session.
  reset();install();assert(host::requests==0);check();workerUntilResult();assert(status().state==State::Available && host::begins==0);
  reset();host::image[512]='x';attemptInstall();assert(status().state==State::Failed && host::aborts==1 && host::boots==0);
  reset();host::truncated=4096;attemptInstall();assert(status().state==State::Failed && host::aborts==1 && host::boots==0);
  reset();host::badImage=true;attemptInstall();assert(status().state==State::Failed && host::boots==0);
  reset();host::response=302;check();workerUntilResult();assert(status().state==State::Failed && host::begins==0);
  reset();WiFi.connected=0;check();workerUntilResult();assert(status().state==State::Failed && host::requests==0);
  reset();host::feed="{}";check();workerUntilResult();assert(status().state==State::Failed && host::begins==0);
  reset();parts[3].size-=4096;check();workerUntilResult();assert(status().state==State::Failed && host::requests==0);parts[3].size+=4096;
  reset();attemptInstall();assert(status().state==State::Restarting && host::boots==1 && host::written==validImage);
  close();assert(enabled());delay(6000);poll();assert(ESP.restarts==1);
  host::otaState=ESP_OTA_IMG_PENDING_VERIFY;confirmBoot(false);assert(host::rolledBack==1 && host::confirmed==0);
  confirmBoot(true);assert(host::confirmed==1);
  puts("OTA runtime: opt-in, compatible feed, TLS policy, Wi-Fi failure, inactive slot, truncation, SHA-256 corruption, invalid image, restart and boot confirmation verified");
}
