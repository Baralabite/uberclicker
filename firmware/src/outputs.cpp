#include "outputs.h"
#include "config_service.h"
#include "output_codec.h"
#include "output_safety.h"
#include "socket_output.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <WiFi.h>
#include <atomic>
#include <host/ble_store.h>
#include <esp_random.h>
namespace outputs {
static BLEServer *server;
static BLECharacteristic *keyboard, *midi;
static std::atomic<bool> connected{false}, encrypted{false}, subscribedKey{false}, subscribedMidi{false};
static std::atomic<bool> restartAdvertising{false}, resetRelease{true}, paused{true};
static std::atomic<bool> notified{false}, active{false}, physicalSetup{false}, bondFault{false};
static std::atomic<uint32_t> generation{0}, passkey{0};
static std::atomic<uint16_t> connection{0xffff};
static QueueHandle_t jobs, results, wifiChanges, dnsChanges;
static portMUX_TYPE dnsLock=portMUX_INITIALIZER_UNLOCKED;
static char cachedHost[64]={};static uint32_t cachedAddress=0;static bool dnsReady=false;
static bool resolve(const char *host,IPAddress &ip){
 if(ip.fromString(host))return true;
 portENTER_CRITICAL(&dnsLock);bool ready=dnsReady&&!strcmp(host,cachedHost);uint32_t address=cachedAddress;portEXIT_CRITICAL(&dnsLock);
 if (ready)
   ip = IPAddress(address);
 return ready;
}
static void dnsTask(void *) {
 char host[64]={};uint32_t last=0;
 for(;;){
  bool changed=xQueueReceive(dnsChanges,host,pdMS_TO_TICKS(500))==pdPASS;
  if(WiFi.status()!=WL_CONNECTED){portENTER_CRITICAL(&dnsLock);dnsReady=false;portEXIT_CRITICAL(&dnsLock);last=0;continue;}
  if(!host[0]||(!changed&&last&&uint32_t(millis()-last)<10000))continue;
  uint32_t epoch=generation;IPAddress ip;bool ready=WiFi.hostByName(host,ip)==1;last=millis();ready=ready&&WiFi.status()==WL_CONNECTED;
  portENTER_CRITICAL(&dnsLock);if(epoch==generation&&!strcmp(host,cachedHost)){cachedAddress=uint32_t(ip);dnsReady=ready;}portEXIT_CRITICAL(&dnsLock);
 }
}
struct Wifi { char ssid[33], password[64]; };
struct Job { uint8_t bytes[160]; size_t size; char host[64]; uint16_t port; bool tcp; uint32_t at, generation; };
static output_safety::Release release;
static uint32_t releaseAt=0;
static uint8_t releaseChannel=1;

class ServerCallbacks final : public BLEServerCallbacks {
 void onConnect(BLEServer *,ble_gap_conn_desc *desc) override {
  if(connection!=0xffff){server->disconnect(desc->conn_handle);return;}
  connection=desc->conn_handle; connected=true; encrypted=false;subscribedKey=false;subscribedMidi=false;resetRelease=true;
 }
 void onDisconnect(BLEServer *,ble_gap_conn_desc *desc) override {
  if(connection!=desc->conn_handle)return;
  uc::configService.onDisconnect(desc->conn_handle);connection=0xffff;connected=false;encrypted=false;subscribedKey=false;subscribedMidi=false;
  resetRelease=true;restartAdvertising=true;
 }
};
class Security final : public BLESecurityCallbacks {
 bool onSecurityRequest() override { return physicalSetup; }
 void onPassKeyNotify(uint32_t) override { if(!physicalSetup&&connection!=0xffff)server->disconnect(connection); }
 uint32_t onPassKeyRequest() override { return passkey; }
 bool onConfirmPIN(uint32_t) override { return false; }
 void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
  if(connection!=desc->conn_handle)return;
  encrypted=desc->sec_state.encrypted&&desc->sec_state.authenticated;
  uc::configService.onSecurity(desc->conn_handle,desc->sec_state.encrypted,desc->sec_state.authenticated);
 }
};
class Reports final : public BLECharacteristicCallbacks {
 void onSubscribe(BLECharacteristic *c,ble_gap_conn_desc *desc,uint16_t v) override {
  if(connection!=desc->conn_handle)return;
  if(c==keyboard)subscribedKey=(v&1)!=0;else subscribedMidi=(v&1)!=0;
  resetRelease=true;
 }
 void onStatus(BLECharacteristic *,Status status,uint32_t) override { notified=status==SUCCESS_NOTIFY; }
};
static bool notify(BLECharacteristic *c,const uint8_t *bytes,size_t n,bool subscribed) {
 if(!connected||!encrypted||!subscribed)return false;
 c->setValue(bytes,n);notified=false;c->notify();return notified;
}
static bool keyReport(uint8_t key=0,uint8_t modifiers=0) {
 uint8_t bytes[8]={modifiers,0,key};return notify(keyboard,bytes,8,subscribedKey);
}
static bool midiReport(const uint8_t *bytes,size_t n) {
 uint16_t stamp=millis()&8191;uint8_t packet[5]={uint8_t(0x80|(stamp>>7)),uint8_t(0x80|(stamp&127)),0,0,0};
 memcpy(packet+2,bytes,n);return notify(midi,packet,n+2,subscribedMidi);
}
static bool allNotesOff() {
 uint8_t message[3];
 // One channel per poll: restarting a 16-notification burst can starve later channels.
 output_codec::midi(1,releaseChannel,123,0,false,message);
 if(!midiReport(message,3))return false;
 if(++releaseChannel<=16)return false;
 releaseChannel=1;release.note=false;return true;
}
static void networkTask(void *) {
 Wifi wifi{};Job job;WiFi.persistent(false);
 WiFi.onEvent([](WiFiEvent_t event){
  if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED||event==ARDUINO_EVENT_WIFI_STA_GOT_IP){
   ++generation;portENTER_CRITICAL(&dnsLock);dnsReady=false;portEXIT_CRITICAL(&dnsLock);
  }
 });
 for(;;) {
  Wifi changed;
  if(xQueueReceive(wifiChanges,&changed,0)==pdPASS) {
   wifi=changed;WiFi.disconnect(false,false);
   if(wifi.ssid[0]){WiFi.mode(WIFI_STA);WiFi.begin(wifi.ssid,wifi.password);}
  }
  if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(10))!=pdPASS)continue;
  Result result=Result::Rejected;
  auto current=[](void *context){auto &j=*static_cast<Job*>(context);return output_safety::current(paused,j.generation,generation,j.at,millis());};
  IPAddress ip;
  if(current(&job)&&WiFi.status()==WL_CONNECTED&&resolve(job.host,ip)) {
   auto sent=socket_output::send(job.tcp,uint32_t(ip),job.port,job.bytes,job.size,current,&job);
   result=sent==socket_output::Result::Sent?Result::Sent:sent==socket_output::Result::Unknown?Result::Unknown:Result::Rejected;
  }
  xQueueOverwrite(results,&result); // exactly one active job, never replay after reconnect
 }
}
BLEServer *begin(const char *name) {
 BLEDevice::init(name);securityPasskey(100000+esp_random()%900000);BLEDevice::setSecurityCallbacks(new Security());
 BLESecurity::setAuthenticationMode(true,true,true);BLESecurity::setCapability(ESP_IO_CAP_OUT);
 server=BLEDevice::createServer();server->setCallbacks(new ServerCallbacks());
 auto hid=new BLEHIDDevice(server);keyboard=hid->inputReport(1);keyboard->setCallbacks(new Reports());hid->outputReport(1);
 hid->manufacturer()->setValue("UberClicker");hid->pnp(2,0,0,0x0100);hid->hidInfo(0,1);hid->setBatteryLevel(constrain((int(analogReadMilliVolts(8))*2-3300)/45,0,20)*5);
 static uint8_t map[]={0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0};
 hid->reportMap(map,sizeof(map));hid->startServices();
 auto service=server->createService("03b80e5a-ede8-4b33-a751-6ce34ec4c700");
 midi=service->createCharacteristic("7772e5db-3868-4112-a1a9-f2669d106bf3",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE_NR|BLECharacteristic::PROPERTY_NOTIFY);
 midi->setCallbacks(new Reports());service->start();
 auto advertising=server->getAdvertising();
 // Split UUID lists across the 31-byte advertisement and active scan response.
 BLEAdvertisementData advertisement, scanResponse;
 advertisement.setFlags(0x06);advertisement.setAppearance(HID_KEYBOARD);
 advertisement.setPartialServices(BLEUUID(uc::kConfigServiceUuid));
 advertisement.setPartialServices(BLEUUID(uint16_t(0x1812)));
 scanResponse.setPartialServices(BLEUUID("03b80e5a-ede8-4b33-a751-6ce34ec4c700"));
 scanResponse.setName(name);advertising->setScanResponse(true);
 if(advertisement.getPayload().length()>31||scanResponse.getPayload().length()>31||
    !advertising->setAdvertisementData(advertisement)||!advertising->setScanResponseData(scanResponse))return nullptr;
 jobs=xQueueCreate(1,sizeof(Job));results=xQueueCreate(1,sizeof(Result));wifiChanges=xQueueCreate(1,sizeof(Wifi));
 dnsChanges=xQueueCreate(1,64);
 if(jobs&&results&&wifiChanges&&dnsChanges){
  if(xTaskCreate(networkTask,"outputs",4096,nullptr,1,nullptr)!=pdPASS)jobs=nullptr;
  if(xTaskCreate(dnsTask,"dns",4096,nullptr,1,nullptr)!=pdPASS)dnsChanges=nullptr;
 }else jobs=nullptr;
 return server;
}
void securityPasskey(uint32_t key) { if(!key)return;passkey=key;BLESecurity::setPassKey(true,key); }
void clearBonds() {
 pause(true);
 if(connection!=0xffff)server->disconnect(connection);
 bondFault=ble_store_clear()!=0;
 if(bondFault)Serial.println("BLE bond removal failed; reset requires retry");
}
bool bondsCleared(){return !bondFault;}
void setupWindow(bool open){physicalSetup=open;}
bool forgetWifi(){bool ok=WiFi.mode(WIFI_STA)&&WiFi.eraseAP();WiFi.mode(WIFI_OFF);return ok;}
void configureEndpoint(const char *host){
 char name[64]={};strlcpy(name,host,sizeof(name));
 portENTER_CRITICAL(&dnsLock);strlcpy(cachedHost,name,sizeof(cachedHost));dnsReady=false;portEXIT_CRITICAL(&dnsLock);
 if(dnsChanges)xQueueOverwrite(dnsChanges,name);
}
void configureWifi(const char *ssid,const char *password) {
 Wifi value{};strlcpy(value.ssid,ssid,sizeof(value.ssid));strlcpy(value.password,password,sizeof(value.password));
 if(wifiChanges)xQueueOverwrite(wifiChanges,&value);
}
void pause(bool value) {
 value=value||bondFault;if(paused.exchange(value)!=value){++generation;if(value){release.pause();releaseChannel=1;}}
}
bool busy(){return active||release.keyboard||release.midi||release.note;}
void poll() {
 if(restartAdvertising.exchange(false))BLEDevice::startAdvertising();
 if(resetRelease.exchange(false)){release.reconnect();releaseChannel=1;}
 if(uint32_t(millis()-releaseAt)<30)return;
 releaseAt=millis();
 if(release.keyboard&&connected&&encrypted&&subscribedKey)release.keyboard=!keyReport();
 if(release.midi&&connected&&encrypted&&subscribedMidi)release.midi=!allNotesOff();
 if(release.note){uint8_t bytes[3];output_codec::midi(0,release.channel,release.number,0,true,bytes);if(midiReport(bytes,3))release.note=false;}
}
bool takeResult(Result &result) {if(!results||xQueueReceive(results,&result,0)!=pdPASS)return false;active=false;return true;}
Result submit(JsonObjectConst profile,bool next) {
 if(paused||active)return Result::Rejected;
 JsonObjectConst action=profile[next?"next":"previous"];
 const char *output=profile["output"]|"";
 if(!strcmp(output,"ble_keyboard")) {
  if(!release.canKey()||!connected||!encrypted||!subscribedKey)return Result::Rejected;
  bool ok=keyReport(action["key"],action["modifiers"]);release.keyboard=true;releaseAt=millis();return ok?Result::Submitted:Result::Unknown;
 }
 if(!strcmp(output,"ble_midi")) {
  if(!release.canMidi()||!connected||!encrypted||!subscribedMidi)return Result::Rejected;
  JsonObjectConst mapping=action["midi"];const char *type=mapping["type"]|"";if(strcmp(type,"note")&&strcmp(type,"cc")&&strcmp(type,"program"))return Result::Rejected;uint8_t kind=!strcmp(type,"note")?0:!strcmp(type,"cc")?1:2;
  uint8_t bytes[3];size_t size=output_codec::midi(kind,mapping["channel"],mapping["number"],mapping["value"],false,bytes);
  if(!size)return Result::Rejected;
  bool ok=midiReport(bytes,size);
  if(kind==0){release.note=true;release.channel=mapping["channel"];release.number=mapping["number"];releaseAt=millis();}
  return ok?Result::Submitted:Result::Unknown;
 }
 if(!jobs||!results)return Result::Rejected;
 JsonString payload=action["payload"].as<JsonString>();JsonString addressText=action["osc"].as<JsonString>();
 if((payload.c_str()&&payload.size()!=strlen(payload.c_str()))||(addressText.c_str()&&addressText.size()!=strlen(addressText.c_str())))return Result::Rejected;
 Job job{};job.tcp=!strcmp(output,"tcp");strlcpy(job.host,profile["host"]|"",sizeof(job.host));job.port=profile["port"]|0;
 IPAddress address;
 if(!job.host[0]||!job.port||WiFi.status()!=WL_CONNECTED||!resolve(job.host,address))return Result::Rejected;
 if(!strcmp(output,"osc_udp"))job.size=output_codec::osc(action["osc"]|"",action["midi"]["value"]|0,job.bytes,sizeof(job.bytes));
 else if(job.tcp||!strcmp(output,"udp"))job.size=output_codec::raw(action["payload"]|"",job.tcp,job.bytes,sizeof(job.bytes));
 if(!job.size)return Result::Rejected;
 job.at=millis();job.generation=generation;active=true;
 if(xQueueSend(jobs,&job,0)!=pdPASS){active=false;return Result::Rejected;}
 return Result::Pending;
}
}
