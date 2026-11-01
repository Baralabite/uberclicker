#include "config_service.h"
#include "ble_name.h"
#include "device_ui.h"
#include "ota_update.h"
#include "outputs.h"
#include "cloud_output.h"
#include "radio_broadcast.h"
#include "reset_journal.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
extern "C" bool verifyRollbackLater() { return true; }
static SX1262 radio = new Module(17,4,6,5);
static device_ui::Controller ui;
static device_ui::Snapshot snapshot;
static radio_broadcast::Listener listener;
static bool tx=true, direct=false, radioReady=false, transmitting=false, configPaused=true, reconfigure=false, updateScreenRequested=false;
static volatile bool irq=false;
static uint64_t deviceId=0;
static uint16_t bluetoothSuffix=0;
static uint32_t bootId=0, sequence=0, sentAt=0, airMs=0, beaconAt=0;
static unsigned profileIndex=0;
static uint8_t radioSf=0;
static void IRAM_ATTR radioInterrupt(){irq=true;}
static bool expander(uint8_t reg,uint8_t value){Wire1.beginTransmission(0x18);Wire1.write(reg);Wire1.write(value);return Wire1.endTransmission()==0;}
static JsonObjectConst profile(){return uc::configService.settings()["profiles"][profileIndex].as<JsonObjectConst>();}
static device_ui::Outcome outcome(outputs::Result r){return r==outputs::Result::Pending?device_ui::Outcome::Pending:r==outputs::Result::Submitted?device_ui::Outcome::Submitted:r==outputs::Result::Sent?device_ui::Outcome::Sent:r==outputs::Result::Unknown?device_ui::Outcome::Unknown:device_ui::Outcome::Rejected;}
static bool paused(){return ota_update::enabled()||configPaused||uc::configService.outputPaused()||ui.cuesPaused();}
static void pauseOutput(bool v){configPaused=v;outputs::pause(v||ui.cuesPaused());}
static void changed(){reconfigure=true;}
static void statusChanged(const uc::ConfigStatus &s){outputs::securityPasskey(s.passkey);outputs::setupWindow(s.setup);}
static void updateBluetoothName(){
  char name[ble_name::Capacity];
  const char *deviceName=profile()["name"]|"";
  ble_name::format(name,sizeof(name),tx,deviceName,bluetoothSuffix);
  if(outputs::configureName(name))strlcpy(snapshot.bluetoothName,name,sizeof(snapshot.bluetoothName));
}
static void applySettings(){
  outputs::pause(true);
  const auto &c=uc::configService.settings();tx=c["role"]!="rx";profileIndex=c["activeProfile"]|0;
  updateBluetoothName();
  outputs::configureEndpoints(profile());
  bool cloudEnabled=false;
  for(const char *event:{"previous","next"})for(auto action:profile()[event].as<JsonArrayConst>())
    cloudEnabled |= tx && action["enabled"] == true && action["type"] == "cloud";
  cloud_output::configure(uc::configService.cloudRoom(),uc::configService.cloudToken(),cloudEnabled,String(profile()["name"]|"M5 clicker"));
  std::vector<outputs::WifiNetwork> networks;
  for(auto saved:c["wifi"].as<JsonArrayConst>()) {
    outputs::WifiNetwork network{};
    strlcpy(network.ssid,saved["ssid"]|"",sizeof(network.ssid));
    strlcpy(network.password,uc::configService.wifiPassword(network.ssid).c_str(),sizeof(network.password));
    networks.push_back(network);
  }
  outputs::configureWifi(networks);
  uint8_t sf=c["radio"]=="long_range"?12:7;
  if(!radioReady||sf!=radioSf){
    if(transmitting){radio.finishTransmit();transmitting=false;}
    radioReady=radio.begin(917.0,125.0,sf,5,RADIOLIB_SX126X_SYNC_WORD_PRIVATE,10,8,3.3)==RADIOLIB_ERR_NONE;
    if(radioReady)radioReady=radio.setDio2AsRfSwitch(true)==RADIOLIB_ERR_NONE&&radio.setCRC(2)==RADIOLIB_ERR_NONE&&radio.setCurrentLimit(140)==RADIOLIB_ERR_NONE&&radio.setRxBoostedGainMode(true)==RADIOLIB_ERR_NONE;
    if(radioReady){radioSf=sf;radio.setDio1Action(radioInterrupt);radio.startReceive();}
  }
  reconfigure=false;outputs::pause(paused());
}
static bool broadcast(uint8_t direction){
  if(!tx||!radioReady||transmitting)return false;
  radio_broadcast::Packet p;p.id=deviceId;p.boot=bootId;p.sequence=++sequence;p.direction=direction;
  if(!sequence){bootId=esp_random();if(!bootId)bootId=1;p.boot=bootId;p.sequence=++sequence;}
  strlcpy(p.name,profile()["name"]|"Clicker",sizeof(p.name));
  uint8_t bytes[radio_broadcast::Size];radio_broadcast::encode(p,bytes);
  radio.standby();irq=false;airMs=(radio.getTimeOnAir(sizeof(bytes))+999)/1000;
  transmitting=radio.startTransmit(bytes,sizeof(bytes))==RADIOLIB_ERR_NONE;sentAt=millis();beaconAt=sentAt;
  if(!transmitting)radio.startReceive();return transmitting;
}
static void action(bool next){
  if(!tx||paused())return;
  snapshot.outcome=outcome(outputs::submit(profile(),next,[](bool next){return broadcast(next?2:1);},[]{return !transmitting;}));
}
static void saveSelection(){
  Preferences selection;
  if(selection.begin("uc-listener",false)){ selection.putULong64("selected",listener.selected); selection.end(); }
}
static void pollRadio(){
  if(!radioReady)return;
  if(transmitting){if(irq||uint32_t(millis()-sentAt)>airMs+200){radio.finishTransmit();transmitting=false;irq=false;radio.startReceive();}return;}
  if(irq){irq=false;uint8_t bytes[radio_broadcast::Size];size_t n=radio.getPacketLength();
    int state=n==sizeof(bytes)?radio.readData(bytes,n):RADIOLIB_ERR_UNKNOWN;
    int16_t rssi=radio.getRSSI();radio.startReceive();radio_broadcast::Packet packet;
    if(!tx&&state==RADIOLIB_ERR_NONE&&radio_broadcast::decode(bytes,n,packet)){
      uint64_t previousSelection=listener.selected;
      bool cue=listener.receive(packet,rssi,millis());
      if(listener.selected!=previousSelection)saveSelection();
      if(cue&&!paused())snapshot.outcome=outcome(outputs::submit(profile(),packet.direction==2));
    }
  }
  if(tx&&!paused()&&!outputs::eventActive()&&uint32_t(millis()-beaconAt)>10000+uint32_t(deviceId%2000))broadcast(0);
}
static void updateSnapshot(){
  updateBluetoothName();
  auto s=uc::configService.status();snapshot.role=tx?device_ui::Role::Tx:device_ui::Role::Rx;
  direct=false;
  const char *mode="";bool multiple=false;
  for(const char *event:{"previous","next"})for(auto a:profile()[event].as<JsonArrayConst>())if(a["enabled"]==true && a["type"]!="lora") {
    direct=true;const char *type=a["type"]|"";
    if(mode[0]&&strcmp(mode,type))multiple=true;else mode=type;
  }
  snapshot.route=direct?device_ui::Route::Direct:device_ui::Route::LoRa;
  snapshot.setup=s.editing?device_ui::Setup::Editing:s.setup?device_ui::Setup::Pairing:device_ui::Setup::None;
  snapshot.paused=configPaused;snapshot.recoveryRequired=s.recoveryRequired;snapshot.update=ota_update::status();
  snapshot.receiverRosterKnown=true;snapshot.linkMonitoringAvailable=radioReady;
  snprintf(snapshot.cueReceiverId,sizeof(snapshot.cueReceiverId),"%012llx",(unsigned long long)listener.selected);
  const uint32_t now=millis();
  for(unsigned i=0;i<8;++i){auto &row=snapshot.receivers[i];row=device_ui::ReceiverLink{};const auto &heard=listener.seen[i];
    if(!tx&&listener.recent(heard,now)){row.paired=true;row.fresh=true;row.signalValid=true;row.state=device_ui::ReceiverState::Connected;row.rssiDbm=heard.rssi;
      row.lastSeenSeconds=uint32_t(now-heard.at)/1000;
      snprintf(row.deviceId,sizeof(row.deviceId),"%012llx",(unsigned long long)heard.packet.id);strlcpy(row.name,heard.packet.name,sizeof(row.name));}}
  const char *activeName=profile()["name"]|"";
  if(activeName[0])strlcpy(snapshot.deviceName,activeName,sizeof(snapshot.deviceName));
  else snprintf(snapshot.deviceName,sizeof(snapshot.deviceName),"%04X",unsigned(bluetoothSuffix));
  strlcpy(snapshot.output,multiple?"actions":mode,sizeof(snapshot.output));
  snapshot.wifiConnected=WiFi.status()==WL_CONNECTED;
  cloud_output::pairingCode(snapshot.cloudCode,sizeof(snapshot.cloudCode));
  strlcpy(snapshot.setupLine1,s.editing?"USB serial setup":"Keyboard / MIDI pairing",sizeof(snapshot.setupLine1));
  if(s.editing) strlcpy(snapshot.setupLine2,"Apply or Cancel in app",sizeof(snapshot.setupLine2));
  else snprintf(snapshot.setupLine2,sizeof(snapshot.setupLine2),"%06lu",(unsigned long)s.passkey);
  static uint32_t batteryAt=0;if(!batteryAt||uint32_t(millis()-batteryAt)>60000){batteryAt=millis();int mv=analogReadMilliVolts(8)*2;snapshot.battery=mv<2500?device_ui::Battery::Unknown:mv<3500?device_ui::Battery::Low:mv<3900?device_ui::Battery::Medium:device_ui::Battery::High;snapshot.batteryPercent=mv<2500?-1:constrain((mv-3300)*100/900,0,100);}
  ui.setSnapshot(snapshot);device_ui::Frame f;ui.render(f);device_ui::submitDisplay(f);
}
static void pollSerial(){
  static String line;static bool overflow=false;unsigned budget=256;
  while(budget--&&Serial.available()) {char c=Serial.read();if(c=='\n'){
    if(!overflow&&line.startsWith("UC:")){String reply=uc::configService.processSerialRequest(line.substring(3));Serial.print("UC:");Serial.println(reply);}
    line="";overflow=false;
  }else if(c!='\r'){if(line.length()<uc::kConfigMaxMessage+3)line+=c;else overflow=true;}}
}
void setup() {
  Serial.begin(115200);
  Serial.printf("UberClicker %s starting\n", UBERCLICKER_VERSION);
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  pinMode(21, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  Wire1.begin(48, 47);
  bool board = expander(1, 1 << 4) && expander(3, 0xc1);
  pinMode(46, OUTPUT);
  digitalWrite(46, HIGH);
  // GPS remains powered down; no GPS UART, tracker, or upload code is linked.
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  pinMode(11, OUTPUT);
  digitalWrite(11, LOW);
  delay(100);
  bool displayReady = device_ui::beginDisplay();
  Serial.printf("Display task %s\n", displayReady ? "ready" : "allocation failed");
  SPI.begin(16, 7, 15, 17);
  deviceId = ESP.getEfuseMac() & 0xffffffffffffULL;
  char id[13];
  snprintf(id, sizeof(id), "%012llx", (unsigned long long)deviceId);
  uint8_t bluetoothMac[6];
  ESP_ERROR_CHECK(esp_read_mac(bluetoothMac, ESP_MAC_BT));
  bluetoothSuffix=(uint16_t(bluetoothMac[4])<<8)|bluetoothMac[5];
  ble_name::format(snapshot.bluetoothName,sizeof(snapshot.bluetoothName),true,"",bluetoothSuffix);
  char defaultDeviceName[5];snprintf(defaultDeviceName,sizeof(defaultDeviceName),"%04X",unsigned(bluetoothSuffix));
  BLEServer *server = outputs::begin(snapshot.bluetoothName);
  Preferences selection;
  if (selection.begin("uc-listener", true)) { listener.selected=selection.getULong64("selected",0); selection.end(); }
  bootId=esp_random(); if(!bootId)bootId=1;
  uc::ConfigHooks hooks;
  hooks.pauseOutput = pauseOutput;
  hooks.changed = changed;
  hooks.clearBonds = outputs::clearBonds;
  hooks.clearCloudAccount = cloud_output::forgetAccount;
  hooks.statusChanged = statusChanged;
  hooks.openUpdates = []() {
    if (ota_update::enabled() || transmitting || outputs::busy()) return false;
    updateScreenRequested = true;
    return true;
  };
  if (!board || !server ||
      !uc::configService.begin(id, UBERCLICKER_VERSION, hooks, defaultDeviceName)) {
    ota_update::confirmBoot(false);
    Serial.println("Hardware/config initialization failed; outputs disabled");
    for (;;)
      delay(1000);
  }
  // Finish a reset before advertising or restoring any configured Wi-Fi
  // connection.
  auto resetState = reset_journal::state();
  if (resetState == reset_journal::State::Unavailable) {
    ota_update::confirmBoot(false);
    Serial.println("Reset journal unavailable; outputs locked");
    for (;;)
      delay(1000);
  }
  if (resetState == reset_journal::State::Pending) {
    Serial.println("Completing requested factory reset");
    bool cleared = uc::configService.factoryReset() &&
                   outputs::bondsCleared() && outputs::forgetWifi() && cloud_output::forgetAccount();
    if (!cleared || !reset_journal::mark(false)) {
      ota_update::confirmBoot(false);
      Serial.println("Reset incomplete; restart to retry, outputs locked");
      for (;;)
        delay(1000);
    }
    Serial.println("Factory reset complete");
  }
  outputs::securityPasskey(uc::configService.status().passkey);
  applySettings();
  configPaused = uc::configService.outputPaused();
  outputs::pause(configPaused);
  BLEDevice::startAdvertising();
  ota_update::confirmBoot(radioReady || direct);
  updateSnapshot();
  Serial.printf("Startup complete: %s, radio %s, display %s\n", tx ? "TX" : "RX",
                radioReady ? "ready" : "unavailable",
                displayReady ? "frame queued" : "unavailable");
}
void loop() {
  ota_update::poll();

  uc::configService.poll();
  if (updateScreenRequested) {
    updateScreenRequested = false;
    ota_update::open();
    uc::configService.exitSetup();
    outputs::pause(true);
  }
  if (reconfigure)
    applySettings();
  auto event =
      ui.poll(digitalRead(21) == LOW, digitalRead(14) == LOW, millis());
  switch (event) {
  case device_ui::Action::Previous:
    action(false);
    break;
  case device_ui::Action::Next:
    action(true);
    break;
  case device_ui::Action::OpenSetup:
  case device_ui::Action::OpenPairing:

    uc::configService.enterSetup();
    break;
  case device_ui::Action::CloseSetup:
    uc::configService.exitSetup();
    break;
  case device_ui::Action::SelectPrevious:
  case device_ui::Action::SelectNext:
    if(listener.cycle(event==device_ui::Action::SelectNext,millis())) {
      outputs::pause(true);
      saveSelection();
      snapshot.outcome=device_ui::Outcome::None;
      updateSnapshot();
    }
    break;
  case device_ui::Action::CycleProfile:
    profileIndex =
        (profileIndex + 1) % uc::configService.settings()["profiles"].size();
    outputs::configureEndpoints(profile());
    break;
  case device_ui::Action::OpenUpdates:
    uc::configService.exitSetup();
    outputs::pause(true);
    ota_update::open();
    break;
  case device_ui::Action::CloseUpdates:
    ota_update::close();
    break;
  case device_ui::Action::CheckUpdate:
    ota_update::check();
    break;
  case device_ui::Action::InstallUpdate:
    if (snapshot.battery == device_ui::Battery::Low)
      break;
    ota_update::install();
    break;
  case device_ui::Action::FactoryReset:
    outputs::pause(true);
    { Preferences selection; if(selection.begin("uc-listener",false)){selection.clear();selection.end();} }
    if (reset_journal::mark(true))
      ESP.restart();
    else
      snapshot.outcome = device_ui::Outcome::Rejected;
    break;
  default:
    break;
  }
  outputs::pause(paused());
  outputs::poll();
  outputs::Result result;
  if(outputs::takeResult(result)) snapshot.outcome=outcome(result);
  pollRadio();
  pollSerial();
  static uint32_t uiAt=0;
  if(uint32_t(millis()-uiAt)>=100){uiAt=millis();updateSnapshot();}
  delay(1);
}
