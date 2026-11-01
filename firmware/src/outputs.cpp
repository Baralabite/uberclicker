#include "outputs.h"
#include "ble_name.h"
#include "output_codec.h"
#include "output_safety.h"
#include "socket_output.h"
#include "cloud_output.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <WiFi.h>
#include <memory>
#include "wifi_retry.h"
#include <atomic>
#include <host/ble_store.h>
#include <services/gap/ble_svc_gap.h>
#include <esp_random.h>
namespace outputs {
static BLEServer *server;
static char currentName[ble_name::Capacity] = {};
static BLECharacteristic *keyboard, *midi;
static std::atomic<bool> connected{false}, encrypted{false}, subscribedKey{false}, subscribedMidi{false};
static std::atomic<bool> restartAdvertising{false}, resetRelease{true}, paused{true};
static std::atomic<bool> notified{false}, active{false}, physicalSetup{false}, bondFault{false};
static std::atomic<uint32_t> generation{0}, passkey{0};
static std::atomic<uint16_t> connection{0xffff};
static QueueHandle_t jobs, results, wifiChanges, dnsChanges;
static portMUX_TYPE dnsLock=portMUX_INITIALIZER_UNLOCKED;
struct Endpoint { char host[64]; uint32_t address; bool ready; };
struct Endpoints { Endpoint items[16]{}; size_t size = 0; };
static Endpoints cachedEndpoints;
static bool resolve(const char *host, IPAddress &ip) {
 if(ip.fromString(host))return true;
 bool ready=false; uint32_t address=0;
 portENTER_CRITICAL(&dnsLock);
 for(size_t i=0;i<cachedEndpoints.size;++i)if(!strcmp(host,cachedEndpoints.items[i].host)) {
  ready=cachedEndpoints.items[i].ready;address=cachedEndpoints.items[i].address;break;
 }
 portEXIT_CRITICAL(&dnsLock);
 if(ready)ip=IPAddress(address);
 return ready;
}
static void dnsTask(void *) {
 Endpoints endpoints; uint32_t last=0;
 for(;;) {
  bool changed=xQueueReceive(dnsChanges,&endpoints,pdMS_TO_TICKS(500))==pdPASS;
  if(WiFi.status()!=WL_CONNECTED){last=0;continue;}
  if(!changed&&last&&uint32_t(millis()-last)<10000)continue;
  for(size_t i=0;i<endpoints.size;++i) {
   uint32_t epoch=generation;IPAddress ip;
   bool ready=WiFi.hostByName(endpoints.items[i].host,ip)==1 && WiFi.status()==WL_CONNECTED;
   portENTER_CRITICAL(&dnsLock);
   if(epoch==generation && i<cachedEndpoints.size && !strcmp(endpoints.items[i].host,cachedEndpoints.items[i].host)) {
    cachedEndpoints.items[i].address=uint32_t(ip);cachedEndpoints.items[i].ready=ready;
   }
   portEXIT_CRITICAL(&dnsLock);
  }
  last=millis();
 }
}
static ActionSequence sequence;
static bool (*broadcastAction)(bool)=nullptr;
static bool eventNext=false;
static bool (*canBroadcast)()=nullptr;
static Result submitAction(JsonObjectConst action);
using Wifi = std::vector<WifiNetwork>;
struct Job { uint8_t bytes[160]; size_t size; char host[64]; uint16_t port; bool tcp; uint32_t at, generation; };
static output_safety::Release release;
static uint32_t releaseAt=0;
static uint8_t releaseChannel=1;

class ServerCallbacks final : public BLEServerCallbacks {
 void onConnect(BLEServer *,ble_gap_conn_desc *desc) override {
  if(connection!=0xffff){server->disconnect(desc->conn_handle);return;}
  connection=desc->conn_handle; connected=true; encrypted=false;subscribedKey=false;subscribedMidi=false;resetRelease=true;
  Serial.printf("BLE connected: handle=%u\n",desc->conn_handle);
 }
 void onDisconnect(BLEServer *,ble_gap_conn_desc *desc) override {
  if(connection!=desc->conn_handle)return;
  Serial.printf("BLE disconnected: handle=%u\n",desc->conn_handle);
  ++generation;connection=0xffff;connected=false;encrypted=false;subscribedKey=false;subscribedMidi=false;
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
  Serial.printf("BLE security: handle=%u encrypted=%u authenticated=%u\n",desc->conn_handle,desc->sec_state.encrypted,desc->sec_state.authenticated);
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
 std::unique_ptr<Wifi> wifi;WifiRetry retry;Job job;WiFi.persistent(false);
 WiFi.setAutoReconnect(false);
 WiFi.onEvent([](WiFiEvent_t event){
  if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED||event==ARDUINO_EVENT_WIFI_STA_GOT_IP){
   ++generation;portENTER_CRITICAL(&dnsLock);for(size_t i=0;i<cachedEndpoints.size;++i)cachedEndpoints.items[i].ready=false;portEXIT_CRITICAL(&dnsLock);
  }
 });
 for(;;) {
  Wifi *changed;
  if(xQueueReceive(wifiChanges,&changed,0)==pdPASS) {
   wifi.reset(changed);retry.reset();WiFi.disconnect(false,false);
   if(!wifi->empty())WiFi.mode(WIFI_STA);
  }
  size_t index;
  if(wifi && retry.attempt(wifi->size(),WiFi.status()==WL_CONNECTED,millis(),index)) {
   WiFi.disconnect(false,false);
   const auto &network=(*wifi)[index];
   WiFi.begin(network.ssid,network.password);
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
bool configureName(const char *name) {
 if(!strcmp(currentName,name))return true;
 auto advertising=server->getAdvertising();
 BLEAdvertisementData advertisement, scanResponse;
 advertisement.setFlags(0x06);advertisement.setAppearance(HID_KEYBOARD);
 advertisement.setPartialServices(BLEUUID(uint16_t(0x1812)));
 advertisement.setPartialServices(BLEUUID("03b80e5a-ede8-4b33-a751-6ce34ec4c700"));
 size_t length=ble_name::prefixLength(name);
 String advertised(name,length);
 if(length<strlen(name))scanResponse.setShortName(advertised);
 else scanResponse.setName(advertised);
 if(advertisement.getPayload().length()>31||scanResponse.getPayload().length()>31)return false;
 bool wasAdvertising=advertising->isAdvertising();
 if(wasAdvertising&&!advertising->stop())return false;
 // The pinned GAP service permits 31 bytes; scan-response names permit 29.
 String gapName(name,ble_name::prefixLength(name,31));
 bool ok=ble_svc_gap_device_name_set(gapName.c_str())==0&&advertising->setAdvertisementData(advertisement)&&advertising->setScanResponseData(scanResponse);
 advertising->setScanResponse(true);
 if(wasAdvertising&&!advertising->start())ok=false;
 if(ok)strlcpy(currentName,name,sizeof(currentName));
 return ok;
}
BLEServer *begin(const char *name) {
 BLEDevice::init(name);securityPasskey(100000+esp_random()%900000);BLEDevice::setSecurityCallbacks(new Security());
 BLESecurity::setAuthenticationMode(true,true,true);BLESecurity::setCapability(ESP_IO_CAP_OUT);
 server=BLEDevice::createServer();server->setCallbacks(new ServerCallbacks());
 auto hid=new BLEHIDDevice(server);keyboard=hid->inputReport(1);keyboard->setCallbacks(new Reports());hid->outputReport(1);
 hid->manufacturer()->setValue("UberClicker");hid->pnp(2,0,0,0x0100);hid->hidInfo(0,1);hid->setBatteryLevel(constrain((int(analogReadMilliVolts(8))*2-3300)/45,0,20)*5);
 static uint8_t map[]={0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0};
 hid->reportMap(map,sizeof(map));
 // Start advertising after all output services have registered.
 if(!hid->deviceInfo()->start()||!hid->hidService()->start()||!hid->batteryService()->start())return nullptr;
 auto service=server->createService("03b80e5a-ede8-4b33-a751-6ce34ec4c700");
 midi=service->createCharacteristic("7772e5db-3868-4112-a1a9-f2669d106bf3",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE_NR|BLECharacteristic::PROPERTY_NOTIFY);
 midi->setCallbacks(new Reports());if(!service->start())return nullptr;
 if(!configureName(name))return nullptr;
 jobs=xQueueCreate(1,sizeof(Job));results=xQueueCreate(1,sizeof(Result));wifiChanges=xQueueCreate(1,sizeof(Wifi *));
 dnsChanges=xQueueCreate(1,sizeof(Endpoints));
 if(jobs&&results&&wifiChanges&&dnsChanges){
  if(xTaskCreate(networkTask,"outputs",4096,nullptr,1,nullptr)!=pdPASS)jobs=nullptr;
  if(xTaskCreate(dnsTask,"dns",4096,nullptr,1,nullptr)!=pdPASS)dnsChanges=nullptr;
 }else jobs=nullptr;
 cloud_output::begin();
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
bool forgetWifi(){bool ok=WiFi.STA.begin(false)&&WiFi.eraseAP();WiFi.mode(WIFI_OFF);return ok;}
void configureEndpoints(JsonObjectConst profile) {
 Endpoints endpoints;
 for(const char *event:{"previous","next"})for(auto action:profile[event].as<JsonArrayConst>()) {
  const char *host=action["host"]|"";IPAddress ip;
  bool network=action["type"]=="osc_udp"||action["type"]=="udp"||action["type"]=="tcp";
  if(!network||action["enabled"]!=true||!host[0]||ip.fromString(host))continue;
  bool found=false;
  for(size_t i=0;i<endpoints.size;++i)found|=!strcmp(endpoints.items[i].host,host);
  if(!found&&endpoints.size<16)strlcpy(endpoints.items[endpoints.size++].host,host,64);
 }
 ++generation;
 portENTER_CRITICAL(&dnsLock);cachedEndpoints=endpoints;portEXIT_CRITICAL(&dnsLock);
 if(dnsChanges)xQueueOverwrite(dnsChanges,&endpoints);
}
void configureWifi(const std::vector<WifiNetwork> &networks) {
 if(!wifiChanges)return;
 auto value=new Wifi(networks);
 Wifi *old;
 // The main loop is the sole producer; replace any update not consumed yet.
 if(xQueueReceive(wifiChanges,&old,0)==pdPASS)delete old;
 if(xQueueSend(wifiChanges,&value,0)!=pdPASS)delete value;
}
void pause(bool value) {
 value=value||bondFault;if(paused.exchange(value)!=value){++generation;if(value){release.pause();releaseChannel=1;}}
 cloud_output::pause(value);
}
bool eventActive(){return sequence.busy()||active;}
bool busy(){return eventActive()||release.keyboard||release.midi||release.note;}
void poll() {
 if(restartAdvertising.exchange(false))BLEDevice::startAdvertising();
 if(resetRelease.exchange(false)){release.reconnect();releaseChannel=1;}
 if(uint32_t(millis()-releaseAt)>=30) {
  releaseAt=millis();
  if(release.keyboard&&connected&&encrypted&&subscribedKey)release.keyboard=!keyReport();
  if(release.midi&&connected&&encrypted&&subscribedMidi)release.midi=!allNotesOff();
  if(release.note){uint8_t bytes[3];output_codec::midi(0,release.channel,release.number,0,true,bytes);if(midiReport(bytes,3))release.note=false;}
 }
 Result result;
 if(results&&xQueueReceive(results,&result,0)==pdPASS){active=false;sequence.complete(result);}
 if(cloud_output::takeResult(result)){active=false;sequence.complete(result);}
 auto ready=[](JsonObjectConst action) {
  if(active)return false;
  if(action["type"]=="lora" && canBroadcast)return canBroadcast();
  if(action["type"]=="ble_keyboard" && connected&&encrypted&&subscribedKey)return release.canKey();
  if(action["type"]=="ble_midi" && connected&&encrypted&&subscribedMidi)return release.canMidi();
  return true;
 };
 sequence.poll(millis(),generation,paused,submitAction,ready);
}
bool takeResult(Result &result) {return sequence.takeResult(result);}
Result submit(JsonObjectConst profile,bool next,bool (*lora)(bool),bool (*radioReady)()) {
 if(paused||active||sequence.busy())return Result::Rejected;
 broadcastAction=lora;canBroadcast=radioReady;eventNext=next;
 return sequence.start(profile[next?"next":"previous"].as<JsonArrayConst>(),millis(),generation);
}
static Result submitAction(JsonObjectConst action) {
 const char *output=action["type"]|"";
 if(!strcmp(output,"lora"))return broadcastAction&&broadcastAction(eventNext)?Result::Sent:Result::Rejected;
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
 if(!strcmp(output,"cloud")) {
  auto result=cloud_output::submit(eventNext,sequence.issuedAt());
  if(result==Result::Pending)active=true;
  return result;
 }
 if(!jobs||!results)return Result::Rejected;
 JsonString payload=action["payload"].as<JsonString>();JsonString addressText=action["osc"].as<JsonString>();
 if((payload.c_str()&&payload.size()!=strlen(payload.c_str()))||(addressText.c_str()&&addressText.size()!=strlen(addressText.c_str())))return Result::Rejected;
 Job job{};job.tcp=!strcmp(output,"tcp");strlcpy(job.host,action["host"]|"",sizeof(job.host));job.port=action["port"]|0;
 IPAddress address;
 if(!job.host[0]||!job.port||WiFi.status()!=WL_CONNECTED||!resolve(job.host,address))return Result::Rejected;
 if(!strcmp(output,"osc_udp"))job.size=output_codec::osc(action["osc"]|"",action["midi"]["value"]|0,job.bytes,sizeof(job.bytes));
 else if(job.tcp||!strcmp(output,"udp"))job.size=output_codec::raw(action["payload"]|"",job.tcp,job.bytes,sizeof(job.bytes));
 if(!job.size)return Result::Rejected;
 job.at=sequence.issuedAt();job.generation=generation;active=true;
 if(xQueueSend(jobs,&job,0)!=pdPASS){active=false;return Result::Rejected;}
 return Result::Pending;
}
}
