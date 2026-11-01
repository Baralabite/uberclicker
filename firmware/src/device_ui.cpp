#include "device_ui.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "device_ui_font.inc"
#ifndef UBERCLICKER_VERSION
#define UBERCLICKER_VERSION "0.1.0-experimental"
#endif

namespace device_ui {
namespace {
constexpr uint32_t DebounceMs = 25, ResetHoldMs = 3000;
void rect(Frame &f, int x, int y, int w, int h, bool ink = true) {
  for (int yy = y; yy < y+h && yy < 200; ++yy)
    for (int xx = x; xx < x+w && xx < 200; ++xx) if (xx >= 0 && yy >= 0) {
      uint8_t &p = f.pixels[yy*25+xx/8]; uint8_t mask = 0x80 >> (xx%8);
      if (ink) p |= mask; else p &= ~mask;
    }
}
void text(Frame &f, const char *s, int x, int y, int scale = 1, int width = 176, bool ink = true) {
  // Fixed metric clipping with explicit ellipsis; never squeeze labels to fit.
  size_t n = 0; while (n < 32 && s[n]) ++n;
  unsigned cells = width / (6*scale), count = n < cells ? n : cells;
  for (unsigned i = 0; i < count; ++i) {
    char c = n > cells && i >= cells-3 ? '.' : (char)toupper((unsigned char)s[i]);
    const char *p = strchr(kAlphabet, c);
    unsigned glyph = p ? (unsigned)(p-kAlphabet) : 41; // ? for unsupported UTF-8 bytes
    for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
      if (kGlyphs[glyph][row] & (16 >> col)) rect(f, x+i*6*scale+col*scale, y+row*scale, scale, scale, ink);
  }
}
void renderDeviceName(Frame &f, const char *deviceName, int height = 144, int top = 44) {
  char name[33];size_t length=0;
  while(length<32&&deviceName[length])++length;
  memcpy(name,deviceName,length);name[length]=0;
  const char *lines[16];unsigned lengths[16],count=0,longest=0;
  for(unsigned i=0;i<length;) {
    while(i<length&&name[i]==' ')++i;
    if(i==length)break;
    unsigned start=i;
    while(i<length&&name[i]!=' ')++i;
    unsigned n=i-start;
    lines[count]=name+start;lengths[count++]=n;
    if(n>longest)longest=n;
    if(i<length)name[i++]=0;
  }
  if(!count){lines[0]="Device";lengths[0]=longest=6;count=1;}
  char wrapped[16][33];unsigned lineCount=0;
  int scale=176/(6*longest-1);
  if(scale<1)scale=1;
  for(;;--scale) {
    memset(wrapped,0,sizeof(wrapped));lineCount=0;
    for(unsigned i=0;i<count;++i) {
      unsigned used=lineCount?strlen(wrapped[lineCount-1]):0;
      if(used&&(6*(used+1+lengths[i])-1)*scale<=176) {
        wrapped[lineCount-1][used]=' ';
        memcpy(wrapped[lineCount-1]+used+1,lines[i],lengths[i]);
      } else memcpy(wrapped[lineCount++],lines[i],lengths[i]);
    }
    if((9*lineCount-2)*scale<=unsigned(height)||scale==1)break;
  }
  int y=top+(height-int(9*lineCount-2)*scale)/2;
  for(unsigned i=0;i<lineCount;++i) {
    int cells=(176+scale)/(6*scale),length=strlen(wrapped[i]);
    int width=(6*(length<cells?length:cells)-1)*scale;
    text(f,wrapped[i],(200-width)/2,y,scale,cells*6*scale);
    y+=9*scale;
  }
}
uint8_t receiverMask(const Snapshot &s) {
  uint8_t mask=0;
  if (!s.receiverRosterKnown) return mask;
  for (unsigned i=0;i<8;++i) {
    if (!s.receivers[i].paired || !s.receivers[i].deviceId[0]) continue;
    bool duplicate=false;
    for (unsigned j=0;j<i;++j) if ((mask & (1u<<j)) && !strncmp(s.receivers[i].deviceId,s.receivers[j].deviceId,33)) duplicate=true;
    if (!duplicate) mask |= 1u<<i;
  }
  return mask;
}
unsigned signalBars(int16_t dbm) { return dbm<=-115 ? 1 : dbm<=-100 ? 2 : dbm<=-85 ? 3 : 4; }

enum class ModeIcon { Bluetooth, Wifi, Keyboard, Midi, Osc, Udp, Tcp };
void modeIcon(Frame &f, ModeIcon mode, int x, bool connected=true) {
  static const uint16_t pixels[][12]={
    {0x040,0x060,0x050,0x248,0x150,0x0e0,0x0e0,0x150,0x248,0x050,0x060,0x040},
    {0,0x3fc,0xc03,0x801,0,0x1f8,0x204,0,0x0f0,0,0x060,0x060},
    {0,0xfff,0x801,0xd55,0x801,0xaaa,0x801,0x9f9,0x801,0xfff,0,0},
    {0x1f8,0x606,0x801,0x801,0xa05,0x891,0x861,0x801,0x801,0x606,0x1f8,0},
    {0,0,0x180,0x240,0x420,0x801,0x801,0x012,0x00c,0,0,0},
    {0,0xf80,0x880,0x890,0x898,0x8fc,0x898,0x890,0x880,0xf80,0,0},
    {0,0x3c0,0x420,0x420,0x5e0,0x492,0x492,0x07a,0x042,0x042,0x03c,0}
  };
  for(unsigned y=0;y<12;++y)for(unsigned col=0;col<12;++col)
    if(pixels[unsigned(mode)][y]&(0x800>>col))rect(f,x+col,16+y,1,1);
  if(!connected)for(int i=0;i<12;++i)rect(f,x+i,27-i,1,1);
}
void statusBar(Frame &f, const Snapshot &s) {
  bool local=s.role==Role::Rx||s.route==Route::Direct;
  bool keyboard=!strcmp(s.output,"ble_keyboard")||!strcmp(s.output,"BLE keyboard");
  bool midi=!strcmp(s.output,"ble_midi"),osc=!strcmp(s.output,"osc_udp");
  bool udp=!strcmp(s.output,"udp"),tcp=!strcmp(s.output,"tcp");
  bool network=osc||udp||tcp;
  char percent[6];
  if(s.batteryPercent>=0&&s.batteryPercent<=100)snprintf(percent,sizeof(percent),"%d%%",s.batteryPercent);
  else strcpy(percent,"--%");
  text(f,percent,188-int(strlen(percent))*6,20,1,24);
  int x=146;
  if(local&&(keyboard||midi||network)) {
    modeIcon(f,keyboard?ModeIcon::Keyboard:midi?ModeIcon::Midi:osc?ModeIcon::Osc:udp?ModeIcon::Udp:ModeIcon::Tcp,x);x-=16;
  }
  if(local&&(keyboard||midi)){modeIcon(f,ModeIcon::Bluetooth,x);x-=16;}
  if((local&&network)||s.wifiConnected){modeIcon(f,ModeIcon::Wifi,x,s.wifiConnected);x-=16;}
  unsigned bars=s.role==Role::Tx?4:0;
  if(s.role==Role::Rx)for(const auto &p:s.receivers)
    if(p.paired&&p.fresh&&p.signalValid&&!strcmp(p.deviceId,s.cueReceiverId)&&p.rssiDbm>=-160&&p.rssiDbm<=0)bars=signalBars(p.rssiDbm);
  for(unsigned i=0;i<4;++i){int height=3+2*i;rect(f,x+3*i,27-(i<bars?height:1),2,i<bars?height:1);}
  text(f,s.recoveryRequired?"DEVICE":s.deviceName,12,20,1,x-18);
  rect(f,12,34,176,1);
}
void footer(Frame &f, const char *a, const char *b) {
  rect(f,12,166,176,1); text(f,a,12,175); text(f,b,12,188);
}
void title(Frame &f, const char *s) { text(f,s,12,46,2); }
void row(Frame &f, const char *s, unsigned rowNo, unsigned selected) {
  int y = 77 + rowNo*21;
  if (rowNo == selected) rect(f,12,y-4,176,18);
  text(f,s,18,y,1,164,rowNo != selected);
}
}
ReceiverSummary summarizeReceivers(const Snapshot &s) {
  ReceiverSummary summary; summary.rosterKnown=s.receiverRosterKnown; summary.monitoring=s.linkMonitoringAvailable;
  uint8_t mask=receiverMask(s); int32_t total=0;
  for (unsigned i=0;i<8;++i) if (mask & (1u<<i)) {
    const auto &peer=s.receivers[i]; ++summary.paired;
    if (s.linkMonitoringAvailable && peer.state==ReceiverState::Connected && peer.fresh) {
      ++summary.connected;
      if (peer.signalValid && peer.rssiDbm>=-160 && peer.rssiDbm<=0) { total+=peer.rssiDbm; ++summary.samples; }
    } else if (s.linkMonitoringAvailable && peer.state==ReceiverState::Offline) ++summary.offline;
    else ++summary.unknown;
  }
  if (summary.samples) { summary.hasAverage=true; summary.averageRssiDbm=total/summary.samples; summary.averageBars=signalBars(summary.averageRssiDbm); }
  return summary;
}
void Controller::setSnapshot(const Snapshot &s) {
  bool remotePauseChanged = snapshot_.role == Role::Tx && view_ == View::Home &&
    ((s.remote.state == RemoteState::Pending) != (snapshot_.remote.state == RemoteState::Pending));
  if (s.paused != snapshot_.paused || s.setup != snapshot_.setup || s.recoveryRequired != snapshot_.recoveryRequired || remotePauseChanged) {
    if (raw_[0] || raw_[1] || stable_[0] || stable_[1]) draining_ = true;
    hold_ = false;
    if (view_ == View::ResetArmed || view_ == View::ResetChoice) { view_ = View::Home; selection_ = 0; }
  }
  Setup oldSetup = snapshot_.setup;
  bool enteringRecovery = s.recoveryRequired && !snapshot_.recoveryRequired;
  snapshot_ = s;
  if (s.recoveryRequired) {
    if (enteringRecovery || view_ == View::Setup || view_ == View::Updates || view_ == View::Device) { view_ = View::Home; selection_ = 0; }
  } else if (s.update.state != ota_update::State::Off) { view_ = View::Updates; hold_ = false; }
  else if (s.setup == Setup::RemoteEditing) {
    // Physical receiver menus/reset remain available during a remote edit.
    if (view_ == View::Home || oldSetup != Setup::RemoteEditing) view_ = View::Setup;
  } else if (s.setup != Setup::None) { view_ = View::Setup; hold_ = false; }
  else if (oldSetup != Setup::None) view_ = View::Home;
}
bool Controller::cuesPaused() const { return snapshot_.update.state != ota_update::State::Off || snapshot_.paused || snapshot_.recoveryRequired || snapshot_.setup != Setup::None || snapshot_.remote.state == RemoteState::Pending || view_ != View::Home; }
Action Controller::gesture(unsigned b) {
  if (snapshot_.recoveryRequired) {
    if (view_ == View::Home || view_ == View::Setup) {
      if (b == 2) { view_ = View::Menu; selection_ = 0; }
      return Action::None;
    }
    if (view_ == View::Menu) {
      if (b == 2) view_ = View::Home;
      else if (b == 0) selection_ = 1-selection_;
      else { view_ = selection_ == 0 ? View::ResetChoice : View::Home; selection_ = 0; }
      return Action::None;
    }
  }
  if (snapshot_.setup == Setup::RemoteEditing && (view_ == View::Home || view_ == View::Setup)) {
    if (b == 2) { view_ = View::Menu; selection_ = 0; }
    return Action::None;
  }
  if (view_ == View::Updates) {
    using ota_update::State;
    State state = snapshot_.update.state;
    if (state == State::Checking || state == State::Installing || state == State::Restarting) return Action::None;
    if (b != 0) { view_ = View::Home; return Action::CloseUpdates; }
    if (state == State::Available && snapshot_.battery == Battery::Low) return Action::None;
    return state == State::Available ? Action::InstallUpdate : Action::CheckUpdate;
  }
  if (view_ == View::ResetChoice) {
    if (b == 0) selection_ = 1-selection_;
    else if (b == 1 && selection_ == 1) { view_ = View::ResetArmed; hold_ = false; }
    else { view_ = View::Home; selection_ = 0; }
    return Action::None;
  }
  if (view_ == View::ResetArmed) { view_ = View::Home; return Action::None; }
  if (view_ == View::Setup) { view_ = View::Home; return Action::CloseSetup; }
  if (b == 2) { view_ = view_ == View::Home ? View::Menu : View::Home; selection_ = 0; return Action::None; }
  if (view_ == View::Receivers) {
    if (b == 0) { unsigned pages=(summarizeReceivers(snapshot_).paired+2)/3; selection_=(selection_+1)%(pages ? pages : 1); }
    else { view_=View::Menu; selection_=0; }
    return Action::None;
  }
  if (view_ == View::Menu || view_ == View::Device) {
    if (b == 0) { selection_=(selection_+1)%3; return Action::None; }
    unsigned chosen=selection_; selection_=0;
    if (snapshot_.role == Role::Tx) {
      if (chosen==0) { view_=View::Receivers; return Action::None; }
      if (chosen==1) { view_=View::Setup; return Action::OpenSetup; }
      view_=View::ResetChoice;
    } else {
      if (chosen==0) { view_=View::Setup; return Action::OpenSetup; }
      view_=chosen==1 ? View::ResetChoice : View::Home;
    }
    return Action::None;
  }
  if (cuesPaused()) return Action::None;
  if (snapshot_.role == Role::Rx) return summarizeReceivers(snapshot_).paired ? (b == 0 ? Action::SelectPrevious : Action::SelectNext) : Action::None;
  return b == 0 ? Action::Previous : Action::Next;
}
Action Controller::poll(bool b1, bool b2, uint32_t now) {
  bool input[2] = {b1,b2}, released[2] = {};
  if (!ready_) { if (!b1 && !b2) ready_ = true; return Action::None; }
  for (unsigned i=0;i<2;++i) {
    if (input[i] != raw_[i]) { raw_[i] = input[i]; changed_[i] = now; }
    if (stable_[i] != raw_[i] && uint32_t(now-changed_[i]) >= DebounceMs) {
      stable_[i] = raw_[i];
      if (stable_[i]) seen_[i] = true;
      else { released[i] = seen_[i]; seen_[i] = false; }
    }
  }
  if (draining_) {
    if (!b1 && !b2 && !stable_[0] && !stable_[1]) { draining_=false; chord_=false; chordReady_=false; }
    return Action::None;
  }
  // Latch raw overlap before debounce so even a brief chord cannot leak a cue.
  if (b1 && b2) chord_ = true;
  if (chord_) {
    if (stable_[0] && stable_[1]) chordReady_ = true;
    if (view_ == View::ResetArmed) {
      if (!b1 || !b2) { hold_=false; view_=View::Home; draining_=true; return Action::None; }
      if (!hold_ && b1 && b2 && stable_[0] && stable_[1]) { hold_=true; holdStart_=now; }
      if (hold_ && uint32_t(now-holdStart_) >= ResetHoldMs) {
        hold_=false; draining_=true; view_=View::Home; return Action::FactoryReset;
      }
    }
    if (!b1 && !b2 && !stable_[0] && !stable_[1]) {
      bool emit = chordReady_; chord_=false; chordReady_=false;
      return emit ? gesture(2) : Action::None;
    }
    return Action::None;
  }
  if (released[0]) return gesture(0);
  if (released[1]) return gesture(1);
  return Action::None;
}
void Controller::render(Frame &f) const {
  memset(f.pixels,0,sizeof(f.pixels));
  statusBar(f,snapshot_);
  if (view_ == View::Updates) {
    using ota_update::State;
    auto &update = snapshot_.update;
    bool busy = update.state == State::Checking || update.state == State::Installing || update.state == State::Restarting;
    title(f,"Firmware");
    const char *heading = update.state == State::Available ? "INSTALL EXPERIMENTAL?" : update.state == State::Current ? "ALREADY INSTALLED" :
      update.state == State::Checking ? "CHECKING FOR UPDATES" : update.state == State::Installing ? "INSTALLING UPDATE" :
      update.state == State::Restarting ? "RESTARTING" : update.state == State::Failed ? "UPDATE FAILED" : "OTA ENABLED THIS SESSION";
    text(f,heading,12,78);
    const char *version = update.version[0] ? update.version : UBERCLICKER_VERSION;
    char firstLine[30] = {}; size_t length = strlen(version);
    memcpy(firstLine,version,length < 29 ? length : 29); text(f,firstLine,12,96);
    if (length > 29) text(f,version+29,12,107);
    text(f,update.state == State::Available && snapshot_.battery == Battery::Low ? "BATTERY LOW - RECHARGE" : update.state == State::Idle ? "CONFIGURE WI-FI IN APP" : update.detail,12,123);
    text(f,"CUES PAUSED",12,145,2);
    footer(f,busy ? "PLEASE WAIT" : update.state == State::Available ? "B1 INSTALL   B2 EXIT" : "B1 CHECK     B2 EXIT", "KEEP POWER CONNECTED"); return;
  }
  if (view_ == View::Menu && snapshot_.recoveryRequired) {
    title(f,"Recovery menu"); row(f,"Factory reset",0,selection_); row(f,"Back",1,selection_);
    footer(f,"B1 MOVE  B2 SELECT","BOTH HOME / CUES PAUSED"); return;
  }
  if (view_ == View::Menu || view_ == View::Device) {
    title(f,"Menu");
    const char *txMenu[]={"Radio status","Bluetooth pairing","Factory reset"};
    const char *rxMenu[]={"Bluetooth pairing","Factory reset","Back"};
    for (unsigned i=0;i<3;++i) row(f,snapshot_.role==Role::Tx ? txMenu[i] : rxMenu[i],i,selection_);
    footer(f,"B1 MOVE  B2 SELECT","BOTH: HOME / CUES PAUSED"); return;
  }
  if (view_ == View::Receivers) {
    title(f,"Radio broadcast");text(f,"TX SENDS ONLY",12,85);text(f,"SELECT THIS TX ON RX",12,110);
    footer(f,"NO RECEIVER REPLIES","B2 BACK / BOTH HOME");return;
  }
  if (view_ == View::ResetChoice || view_ == View::ResetArmed) {
    title(f,"Factory reset?"); text(f,"WI-FI, BONDS, KEYS,",12,78); text(f,"PROFILES AND APP ACCESS",12,94);
    text(f,"FIRMWARE IS PRESERVED",12,114);
    text(f,view_ == View::ResetArmed ? "HOLD BOTH FOR 3 SECONDS" : selection_ ? "> ERASE SELECTED" : "> CANCEL SELECTED",12,141);
    footer(f,view_ == View::ResetArmed ? "EARLY RELEASE CANCELS" : "B1 CHANGE  B2 SELECT","CUES PAUSED"); return;
  }
  if (snapshot_.recoveryRequired) {
    title(f,"Recovery"); text(f,"SETTINGS NOT LOADED",12,78);
    text(f,"CLAIM / CONFIG BLOCKED",12,103); text(f,"PHYSICAL RESET REQUIRED",12,124);
    text(f,"CUES PAUSED",12,145,2); footer(f,"FIRMWARE IS PRESERVED","BOTH: RECOVERY MENU"); return;
  }
  if (snapshot_.setup == Setup::RemoteEditing) {
    title(f,"Managed by TX");
    text(f,snapshot_.remote.name[0] ? snapshot_.remote.name : "CONTROLLER",12,78);
    text(f,snapshot_.remote.deviceId,12,96,2);
    text(f,"REMOTE EDIT ACTIVE",12,124);
    text(f,"CUES PAUSED",12,145,2);
    footer(f,"B1 / B2: LOCAL STATUS","BOTH: LOCAL MENU"); return;
  }
  bool remoteScreen = snapshot_.role == Role::Tx &&
    (snapshot_.remote.state == RemoteState::Pending ||
      ((snapshot_.setup != Setup::None || snapshot_.paused) &&
        (snapshot_.remote.editing || snapshot_.remote.state != RemoteState::None)));
  if (remoteScreen) {
    const auto &remote = snapshot_.remote;
    title(f,"Receiver setup");
    text(f,remote.name[0] ? remote.name : "RECEIVER",12,77);
    text(f,remote.deviceId,12,94,2);
    const char *status="NO REMOTE RESPONSE", *detail="STATUS UNKNOWN";
    bool apply=remote.operation == RemoteOperation::Apply;
    if (remote.state == RemoteState::Pending) {
      status=apply ? "APPLY PENDING" : "REMOTE REQUEST PENDING"; detail="WAIT FOR RECEIVER RESPONSE";
    } else if (remote.state == RemoteState::Confirmed) {
      status=apply ? "APPLY CONFIRMED" : remote.editing ? "REMOTE EDIT ACTIVE" : "RECEIVER RESPONDED";
      detail=remote.editing ? "RECEIVER CUES PAUSED" : "MATCHED RECEIVER RESPONSE";
    } else if (remote.state == RemoteState::Unreachable) {
      status=apply ? "APPLY UNKNOWN" : "RECEIVER UNREACHABLE";
      detail=apply ? "REREAD BEFORE RETRY" : "NO RESPONSE / STATUS UNKNOWN";
    } else if (remote.state == RemoteState::Unknown) {
      status=apply ? "APPLY UNKNOWN" : "REMOTE OUTCOME UNKNOWN"; detail="REREAD BEFORE RETRY";
    } else if (remote.state == RemoteState::Rejected) {
      status=apply ? "APPLY REJECTED" : "REMOTE REQUEST REJECTED"; detail="RECEIVER REJECTED REQUEST";
    } else if (remote.state == RemoteState::Cached) {
      status="CACHED STATUS ONLY"; detail="NOT A FRESH RESPONSE";
    } else if (remote.editing) { status="REMOTE EDIT ACTIVE"; detail="RECEIVER CUES PAUSED"; }
    if (remote.operation == RemoteOperation::Result) {
      if (remote.state == RemoteState::Pending) { status="RECEIPT LOOKUP PENDING"; detail="NO NEW APPLY SENT"; }
      else if (remote.state == RemoteState::Confirmed) {
        status=remote.resultApplied ? "APPLY CONFIRMED" : "APPLY UNKNOWN";
        detail=remote.resultApplied ? "DURABLE RECEIPT MATCHED" : "NO MATCHING STORED RECEIPT";
      } else if (remote.state == RemoteState::Rejected) { status="RECEIPT LOOKUP REJECTED"; detail="APPLY OUTCOME STILL UNKNOWN"; }
      else if (remote.state != RemoteState::Cached) { status="RECEIPT LOOKUP UNCONFIRMED"; detail="APPLY OUTCOME STILL UNKNOWN"; }
    }
    text(f,status,12,118); text(f,detail,12,136); text(f,"CUES PAUSED",12,151);
    footer(f,"CONFIG TARGET ONLY","CUE ROUTING UNCHANGED");
    if (snapshot_.cueReceiverId[0]) {
      rect(f,12,188,176,8,false); text(f,"CUE RX",12,188); text(f,snapshot_.cueReceiverId,60,188);
    }
    return;
  }
  if (view_ == View::Setup || snapshot_.setup != Setup::None || snapshot_.paused) {
    title(f,snapshot_.setup == Setup::Claim ? "Claim device" : snapshot_.setup == Setup::Pairing ? "Pair Bluetooth" : snapshot_.setup == Setup::Editing ? "Configuring" : "USB setup");
    text(f,snapshot_.bluetoothName,12,78); text(f,snapshot_.setupLine1,12,101);
    unsigned codeLength=0; while(codeLength<32 && snapshot_.setupLine2[codeLength]) ++codeLength;
    text(f,snapshot_.setupLine2,12,119,codeLength>0 && codeLength<=9 ? 3 : 1);
    text(f,"CUES PAUSED",12,145,2); footer(f,"EITHER BUTTON: EXIT","NO OUTPUT WHILE EDITING"); return;
  }
  if (snapshot_.role == Role::Tx && snapshot_.cloudCode[0]) {
    title(f,"Link account");text(f,snapshot_.cloudCode,28,83,3,150);
    text(f,"APP.UBERCLICKER.COM",12,123);text(f,"PORTAL: CUE REMOTE > ADD",12,143);
    footer(f,"CODE EXPIRES IN 10 MIN","BOTH: MENU");return;
  }
  if (snapshot_.role == Role::Rx) {
    unsigned count=0,selected=0,single=0;
    for(unsigned i=0;i<8;++i) if(snapshot_.receivers[i].paired&&snapshot_.receivers[i].fresh) {
      if(!strcmp(snapshot_.receivers[i].deviceId,snapshot_.cueReceiverId))selected=count;
      single=i;
      ++count;
    }
    if(count==1) {
      const auto &p=snapshot_.receivers[single];
      text(f,"Receiving:",40,46,2);
      text(f,"Receiving:",41,46,2);
      renderDeviceName(f,p.name[0]?p.name:"Transmitter",102,70);
      char seen[32];snprintf(seen,sizeof(seen),"Last Seen: %lus",(unsigned long)p.lastSeenSeconds);
      text(f,seen,(200-int(strlen(seen)*6-1))/2,180);
      return;
    }
    unsigned page=selected/5,item=0;
    for(unsigned i=0;i<8;++i) {const auto &p=snapshot_.receivers[i];if(!p.paired||!p.fresh)continue;
      if(item/5==page){int y=44+(item%5)*30;bool chosen=!strcmp(p.deviceId,snapshot_.cueReceiverId);
        if(chosen)rect(f,10,y-3,180,26);
        text(f,p.name[0]?p.name:"Transmitter",14,y,1,172,!chosen);
        char seen[32];snprintf(seen,sizeof(seen),"Last Seen: %lus",(unsigned long)p.lastSeenSeconds);
        text(f,seen,14,y+12,1,172,!chosen);}
      ++item;
    }
    if(!count) {
      text(f,"Waiting for",10,72,2,180);
      text(f,"Transmitters...",10,96,2,180);
    }
  } else {
    renderDeviceName(f,snapshot_.deviceName);
  }
}
} // namespace device_ui

#ifdef ARDUINO
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <atomic>
namespace device_ui {
namespace {
QueueHandle_t frames = nullptr;
std::atomic<uint32_t> refreshes{0};
SPIClass screenSPI(HSPI);
GxEPD2_BW<GxEPD2_154_D67,200> panel(GxEPD2_154_D67(39,40,41,42));
void displayTask(void *) {
  screenSPI.begin(38,-1,45,39);
  for (int pin : {39,40,41}) { pinMode(pin,OUTPUT); digitalWrite(pin,HIGH); }
  panel.epd2.selectSPI(screenSPI,SPISettings(4000000,MSBFIRST,SPI_MODE0));
  panel.init(0,true,10,false); panel.setRotation(0);
  static Frame frame, previous;
  bool first=true; uint32_t lastStart=0;
  for (;;) {
    if (xQueueReceive(frames,&frame,portMAX_DELAY) != pdPASS) continue;
    unsigned left=Frame::Width/8, right=0, top=Frame::Height, bottom=0;
    if (!first) {
      uint32_t elapsed=uint32_t(millis()-lastStart);
      if (elapsed<2500) vTaskDelay(pdMS_TO_TICKS(2500-elapsed));
      // Replace any obsolete frame accumulated during the slow panel refresh.
      xQueueReceive(frames,&frame,0);
      // Compare with the frame actually displayed, after coalescing queued work.
      for (unsigned y=0;y<Frame::Height;++y) for (unsigned x=0;x<Frame::Width/8;++x) {
        unsigned i=y*(Frame::Width/8)+x;
        if (frame.pixels[i]==previous.pixels[i]) continue;
        if (x<left) left=x;
        if (x>=right) right=x+1;
        if (y<top) top=y;
        if (y>=bottom) bottom=y+1;
      }
      if (!right) continue;
    }
    lastStart=millis(); panel.fillScreen(GxEPD_WHITE);
    panel.drawBitmap(0,0,frame.pixels,200,200,GxEPD_BLACK);
    if (first) panel.display(false);
    else {
      panel.displayWindow(left*8,top,(right-left)*8,bottom-top);
      panel.powerOff(); // displayWindow leaves panel driving voltages enabled.
    }
    previous=frame;
    if(first)Serial.println("E-paper first refresh returned");
    first=false; ++refreshes;
  }
}
}
bool beginDisplay() {
  if (frames) return true;
  frames=xQueueCreate(1,sizeof(Frame));
  if (!frames) return false;
  if (xTaskCreate(displayTask,"device-ui",6144,nullptr,1,nullptr) != pdPASS) {
    vQueueDelete(frames); frames=nullptr; return false;
  }
  return true;
}
void submitDisplay(const Frame &f) { if (frames) xQueueOverwrite(frames,&f); }
uint32_t displayRefreshCount() { return refreshes.load(); }
}
#endif
