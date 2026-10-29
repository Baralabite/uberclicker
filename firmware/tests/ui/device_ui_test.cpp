#include "device_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
using namespace device_ui;
struct Harness {
  Controller ui; uint32_t now=100;
  Harness(){ui.poll(false,false,now);}
  Action level(bool a,bool b,uint32_t delay=30){now+=delay;return ui.poll(a,b,now);}
  Action tap(unsigned b){level(b==0,b==1);level(b==0,b==1);level(false,false);return level(false,false);}
  Action both(){level(true,true);level(true,true);level(false,false);return level(false,false);}
  void resetChoice(){both();tap(0);tap(0);tap(1);assert(ui.view()==View::ResetChoice&&ui.selection()==0);}
  void armReset(){resetChoice();tap(0);tap(1);assert(ui.view()==View::ResetArmed);}
};
static void peer(Snapshot &s,unsigned i,ReceiverState state,bool fresh,int rssi){
  auto &p=s.receivers[i];p.paired=true;p.state=state;p.fresh=fresh;p.signalValid=true;p.rssiDbm=rssi;
  snprintf(p.deviceId,sizeof(p.deviceId),"aabbccdd%04u",i+1);snprintf(p.name,sizeof(p.name),"Receiver %u",i+1);
}
static Snapshot roster(unsigned count=3){
  Snapshot s;s.battery=Battery::High;s.receiverRosterKnown=true;s.linkMonitoringAvailable=true;
  for(unsigned i=0;i<count;++i)peer(s,i,ReceiverState::Connected,true,-70-int(i)*4);
  return s;
}
static void save(const char *dir,const char *name,const Controller &ui){
  Frame f;ui.render(f);FILE *out=fopen((std::string(dir)+"/"+name+".pbm").c_str(),"wb");assert(out);
  fprintf(out,"P4\n200 200\n");assert(fwrite(f.pixels,1,Frame::Bytes,out)==Frame::Bytes);fclose(out);
}
static void picture(const char *dir,const char *name,const Snapshot &s){Harness h;h.ui.setSnapshot(s);save(dir,name,h.ui);}
int main(int argc,char **argv){
  Harness a;assert(a.level(true,false)==Action::None);assert(a.level(true,false,5000)==Action::None);
  a.level(false,false);assert(a.level(false,false)==Action::Previous);assert(a.tap(1)==Action::Next);
  Harness bounce;bounce.level(true,false);bounce.level(false,false,5);assert(bounce.level(false,false)==Action::None);
  Controller startup;assert(startup.poll(true,false,0)==Action::None);assert(startup.poll(false,false,100)==Action::None);assert(startup.poll(false,false,130)==Action::None);
  Harness chord;chord.level(true,false);chord.level(true,false);chord.level(true,true,5);chord.level(false,true,5);chord.level(false,true);chord.level(false,false);
  assert(chord.level(false,false)==Action::None&&chord.ui.view()==View::Home);
  chord.both();assert(chord.ui.view()==View::Menu&&chord.ui.cuesPaused());
  assert(chord.tap(1)==Action::None&&chord.ui.view()==View::Receivers);
  assert(chord.tap(0)==Action::None);assert(chord.tap(1)==Action::None&&chord.ui.view()==View::Menu);
  chord.tap(0);assert(chord.tap(1)==Action::OpenPairing&&chord.ui.view()==View::Setup);
  Harness menu;menu.both();for(unsigned i=0;i<3;++i)menu.tap(0);assert(menu.ui.selection()==0);menu.both();assert(menu.ui.view()==View::Home);
  Snapshot rx;rx.role=Role::Rx;Harness receiver;receiver.ui.setSnapshot(rx);assert(receiver.tap(0)==Action::None&&receiver.tap(1)==Action::None);
  receiver.both();assert(receiver.tap(1)==Action::OpenSetup);assert(receiver.tap(0)==Action::CloseSetup);
  Harness cancel;cancel.resetChoice();cancel.level(true,true);assert(cancel.level(true,true,4000)==Action::None);cancel.level(false,false);cancel.level(false,false);assert(cancel.ui.view()==View::Home);
  Harness early;early.armReset();early.level(true,true);early.level(true,true);early.level(true,true,2999);assert(early.level(false,true,1)==Action::None);early.level(false,false);assert(early.level(false,false)==Action::None&&early.ui.view()==View::Home);
  Harness tiny;tiny.armReset();tiny.level(true,true);tiny.level(false,true,5);assert(tiny.ui.view()==View::Home);
  Harness confirm;confirm.armReset();confirm.level(true,true);confirm.level(true,true);assert(confirm.level(true,true,2999)==Action::None);assert(confirm.level(true,true,1)==Action::FactoryReset);assert(confirm.level(true,true,4000)==Action::None);
  Harness rollover;rollover.armReset();rollover.now=0xffffffe0;rollover.level(true,true,0);rollover.level(true,true);assert(rollover.level(true,true,3000)==Action::FactoryReset);
  Harness transition;transition.level(true,false);transition.level(true,false);Snapshot paused;paused.paused=true;transition.ui.setSnapshot(paused);paused.paused=false;transition.ui.setSnapshot(paused);transition.level(false,false);assert(transition.level(false,false)==Action::None);
  Harness window;Snapshot setup;setup.setup=Setup::Editing;setup.paused=true;window.ui.setSnapshot(setup);setup.setup=Setup::App;window.ui.setSnapshot(setup);assert(window.ui.view()==View::Setup&&window.ui.cuesPaused());
  Frame setupFrame;window.ui.render(setupFrame);assert(window.tap(0)==Action::CloseSetup&&window.ui.cuesPaused());Frame awaitingExit;window.ui.render(awaitingExit);assert(!memcmp(setupFrame.pixels,awaitingExit.pixels,Frame::Bytes));
  setup.setup=Setup::None;setup.paused=false;window.ui.setSnapshot(setup);assert(!window.ui.cuesPaused()&&window.tap(1)==Action::Next);

  Snapshot links=roster();auto summary=summarizeReceivers(links);assert(summary.paired==3&&summary.connected==3&&summary.samples==3&&summary.averageRssiDbm==-74&&summary.averageBars==4);
  links.receivers[2].state=ReceiverState::Offline;summary=summarizeReceivers(links);assert(summary.connected==2&&summary.offline==1&&summary.samples==2&&summary.averageRssiDbm==-72&&summary.averageBars==4);
  links.receivers[2].state=ReceiverState::Connected;links.receivers[2].fresh=false;summary=summarizeReceivers(links);assert(summary.connected==2&&summary.unknown==1&&summary.samples==2);
  peer(links,3,ReceiverState::Connected,true,-10);links.receivers[3].paired=false;
  links.receivers[4]=links.receivers[0];links.receivers[4].rssiDbm=-1;
  summary=summarizeReceivers(links);assert(summary.paired==3&&summary.connected==2&&summary.averageRssiDbm==-72); // unpaired and duplicate slots excluded
  links.linkMonitoringAvailable=false;summary=summarizeReceivers(links);assert(summary.connected==0&&summary.unknown==3&&!summary.hasAverage);
  links.receiverRosterKnown=false;summary=summarizeReceivers(links);assert(!summary.rosterKnown&&summary.paired==0&&!summary.hasAverage);
  links=roster();links.receivers[0].rssiDbm=-300;summary=summarizeReceivers(links);assert(summary.connected==3&&summary.samples==2&&summary.averageRssiDbm==-76);
  Harness pager;Snapshot eight=roster(8);pager.ui.setSnapshot(eight);pager.both();pager.tap(1);assert(pager.ui.view()==View::Receivers);pager.tap(0);assert(pager.ui.selection()==1);pager.tap(0);assert(pager.ui.selection()==2);pager.tap(0);assert(pager.ui.selection()==0);assert(pager.tap(1)==Action::None&&pager.ui.view()==View::Menu);
  Harness quiet;Snapshot home=roster();quiet.ui.setSnapshot(home);Frame beforeCue;quiet.ui.render(beforeCue);strcpy(home.lastAction,"Next");strcpy(home.profile,"Different profile");home.outcome=Outcome::Unknown;quiet.ui.setSnapshot(home);Frame afterCue;quiet.ui.render(afterCue);assert(!memcmp(beforeCue.pixels,afterCue.pixels,Frame::Bytes)); // routine Tx home has no action/profile output

  Harness remote;Snapshot admin;admin.setup=Setup::App;admin.paused=true;admin.remote.operation=RemoteOperation::Apply;admin.remote.state=RemoteState::Pending;strcpy(admin.remote.deviceId,"aabbccdd0002");strcpy(admin.cueReceiverId,"aabbccdd0001");remote.ui.setSnapshot(admin);
  Frame pending;remote.ui.render(pending);admin.remote.state=RemoteState::Confirmed;remote.ui.setSnapshot(admin);Frame applied;remote.ui.render(applied);assert(memcmp(pending.pixels,applied.pixels,Frame::Bytes));
  admin.remote.state=RemoteState::Unknown;remote.ui.setSnapshot(admin);Frame unknown;remote.ui.render(unknown);admin.remote.state=RemoteState::Unreachable;remote.ui.setSnapshot(admin);Frame noReply;remote.ui.render(noReply);assert(!memcmp(unknown.pixels,noReply.pixels,Frame::Bytes));
  admin.remote.operation=RemoteOperation::Result;admin.remote.state=RemoteState::Confirmed;remote.ui.setSnapshot(admin);Frame absent;remote.ui.render(absent);admin.remote.resultApplied=true;remote.ui.setSnapshot(admin);Frame found;remote.ui.render(found);assert(memcmp(absent.pixels,found.pixels,Frame::Bytes));
  admin.remote.state=RemoteState::Cached;remote.ui.setSnapshot(admin);Frame cached;remote.ui.render(cached);admin.remote.resultApplied=false;remote.ui.setSnapshot(admin);Frame cachedAgain;remote.ui.render(cachedAgain);assert(!memcmp(cached.pixels,cachedAgain.pixels,Frame::Bytes));
  strcpy(admin.remote.deviceId,"aabbccdd0003");remote.ui.setSnapshot(admin);Frame target;remote.ui.render(target);assert(memcmp(cachedAgain.pixels,target.pixels,Frame::Bytes)&&!strcmp(admin.cueReceiverId,"aabbccdd0001"));
  Harness managed;Snapshot controlled;controlled.role=Role::Rx;controlled.setup=Setup::RemoteEditing;controlled.remote.editing=true;managed.ui.setSnapshot(controlled);assert(managed.tap(0)==Action::None&&managed.tap(1)==Action::None);managed.both();managed.ui.setSnapshot(controlled);assert(managed.ui.view()==View::Menu);managed.tap(0);managed.tap(1);assert(managed.ui.view()==View::ResetChoice);managed.tap(0);managed.tap(1);managed.ui.setSnapshot(controlled);managed.level(true,true);managed.level(true,true);assert(managed.level(true,true,3000)==Action::FactoryReset);
  Harness recovery;Snapshot broken;broken.recoveryRequired=true;recovery.ui.setSnapshot(broken);assert(recovery.ui.cuesPaused()&&recovery.tap(0)==Action::None&&recovery.tap(1)==Action::None);recovery.both();recovery.ui.setSnapshot(broken);recovery.tap(1);assert(recovery.ui.view()==View::ResetChoice&&recovery.ui.selection()==0);recovery.both();assert(recovery.ui.view()==View::Home);recovery.both();recovery.tap(1);recovery.tap(0);recovery.tap(1);recovery.ui.setSnapshot(broken);recovery.level(true,true);recovery.level(true,true);assert(recovery.level(true,true,3000)==Action::FactoryReset);
  Harness update;Snapshot updating;updating.update.state=ota_update::State::Checking;update.ui.setSnapshot(updating);assert(update.ui.view()==View::Updates&&update.ui.cuesPaused());assert(update.tap(0)==Action::None&&update.tap(1)==Action::None);updating.update.state=ota_update::State::Failed;update.ui.setSnapshot(updating);assert(update.tap(0)==Action::CheckUpdate&&update.tap(1)==Action::CloseUpdates);assert(update.ui.cuesPaused()); // core must close the actual update session
  struct Guarded{uint32_t a=0xdeadbeef;Frame f;uint32_t b=0xbeefdead;} guarded;home.role=Role::Rx;home.lastAction[0]=0;memset(home.profile,'X',sizeof(home.profile));quiet.ui.setSnapshot(home);quiet.ui.render(guarded.f);assert(guarded.a==0xdeadbeef&&guarded.b==0xbeefdead);

  if(argc>1){const char *dir=argv[1];Snapshot s;
    picture(dir,"tx-unknown",s);s=roster(0);picture(dir,"tx-none",s);s=roster();picture(dir,"tx-home",s);picture(dir,"tx-all",s);
    s.receivers[2].state=ReceiverState::Offline;picture(dir,"tx-partial",s);s.receivers[2].state=ReceiverState::Connected;s.receivers[2].fresh=false;picture(dir,"tx-stale",s);
    for(auto &p:s.receivers) {
      if(p.paired)p.state=ReceiverState::Offline;
    }
    picture(dir,"tx-offline",s);s.linkMonitoringAvailable=false;picture(dir,"tx-unsupported",s);
    s=roster();s.battery=Battery::Low;picture(dir,"tx-low",s);
    Harness menus;menus.ui.setSnapshot(roster());menus.both();for(unsigned i=0;i<3;++i){char name[32];snprintf(name,sizeof(name),"menu-%u",i);save(dir,name,menus.ui);menus.tap(0);}menus.tap(0);menus.tap(0);menus.tap(1);save(dir,"reset-cancel",menus.ui);menus.tap(0);save(dir,"reset-erase",menus.ui);menus.tap(1);save(dir,"reset-armed",menus.ui);
    Harness pages;pages.ui.setSnapshot(eight);pages.both();pages.tap(1);for(unsigned i=0;i<3;++i){char name[32];snprintf(name,sizeof(name),"receivers-%u",i);save(dir,name,pages.ui);pages.tap(0);}
    Harness mixed;Snapshot mixture=roster();mixture.receivers[1].state=ReceiverState::Offline;mixture.receivers[2].state=ReceiverState::Unknown;mixture.receivers[2].fresh=false;mixed.ui.setSnapshot(mixture);mixed.both();mixed.tap(1);save(dir,"receivers-mixed",mixed.ui);
    Harness rxMenus;rxMenus.ui.setSnapshot(rx);rxMenus.both();for(unsigned i=0;i<3;++i){char name[32];snprintf(name,sizeof(name),"rx-menu-%u",i);save(dir,name,rxMenus.ui);rxMenus.tap(0);}
    Harness unavailable;unavailable.both();unavailable.tap(1);save(dir,"receivers-unknown",unavailable.ui);
    Harness empty;empty.ui.setSnapshot(roster(0));empty.both();empty.tap(1);save(dir,"receivers-none",empty.ui);
    s=Snapshot{};s.role=Role::Rx;picture(dir,"rx-home",s);s.outcome=Outcome::Submitted;strcpy(s.lastAction,"Next");picture(dir,"rx-submitted",s);
    s=Snapshot{};s.setup=Setup::Claim;strcpy(s.setupLine1,"DEMO - NOT A DEVICE CODE");strcpy(s.setupLine2,"------");picture(dir,"claim",s);s.setup=Setup::Pairing;strcpy(s.setupLine1,"Open nearby app");s.setupLine2[0]=0;picture(dir,"pairing",s);s.setup=Setup::Editing;strcpy(s.setupLine1,"Apply or Cancel in app");picture(dir,"editing",s);
    Harness recoveryImage;recoveryImage.ui.setSnapshot(broken);save(dir,"recovery-home",recoveryImage.ui);recoveryImage.both();save(dir,"recovery-menu-0",recoveryImage.ui);recoveryImage.tap(0);save(dir,"recovery-menu-1",recoveryImage.ui);recoveryImage.tap(0);recoveryImage.tap(1);save(dir,"recovery-reset-cancel",recoveryImage.ui);recoveryImage.tap(0);save(dir,"recovery-reset-erase",recoveryImage.ui);recoveryImage.tap(1);save(dir,"recovery-reset-armed",recoveryImage.ui);
    const char *remoteNames[]={"pending","confirmed","unknown","unreachable","cached","editing","rejected"};
    const char *resultNames[]={"pending","found","unknown","unconfirmed","rejected","cached"};
    for(unsigned targetId=0;targetId<2;++targetId){Snapshot r;r.setup=Setup::App;r.paused=true;strcpy(r.cueReceiverId,"aabbccdd0001");strcpy(r.remote.deviceId,targetId?"aabbccdd0003":"aabbccdd0002");strcpy(r.remote.name,targetId?"Stage left":"Stage right");
      for(unsigned state=0;state<7;++state){r.remote.operation=state==3?RemoteOperation::Status:state==5?RemoteOperation::Begin:RemoteOperation::Apply;r.remote.state=state==0?RemoteState::Pending:state==2?RemoteState::Unknown:state==3?RemoteState::Unreachable:state==4?RemoteState::Cached:state==6?RemoteState::Rejected:RemoteState::Confirmed;r.remote.editing=state==5;char name[48];snprintf(name,sizeof(name),"remote-%s-%s",targetId?"left":"right",remoteNames[state]);picture(dir,name,r);}
      for(unsigned state=0;state<6;++state){r.remote.operation=RemoteOperation::Result;r.remote.editing=false;r.remote.resultApplied=state==1;r.remote.state=state==0?RemoteState::Pending:state==3?RemoteState::Unknown:state==4?RemoteState::Rejected:state==5?RemoteState::Cached:RemoteState::Confirmed;char name[48];snprintf(name,sizeof(name),"result-%s-%s",targetId?"left":"right",resultNames[state]);picture(dir,name,r);}
    }
    controlled.remote.editing=true;strcpy(controlled.remote.deviceId,"aabbccdd0010");strcpy(controlled.remote.name,"Presenter TX");picture(dir,"rx-managed",controlled);
    const ota_update::State states[]={ota_update::State::Idle,ota_update::State::Checking,ota_update::State::Available,ota_update::State::Current,ota_update::State::Installing,ota_update::State::Restarting,ota_update::State::Failed};
    const char *stateNames[]={"idle","checking","available","current","installing","restarting","failed"};
    for(unsigned i=0;i<7;++i){Snapshot u;u.update.state=states[i];strcpy(u.update.version,"0.2.0-demo.1");strcpy(u.update.detail,"Review scenario only");char name[32];snprintf(name,sizeof(name),"update-%s",stateNames[i]);picture(dir,name,u);}
  }
  puts("UI tests passed: minimal Tx/menu, fresh paired-only RSSI/counts, paging, buttons/reset, remote receipts and physical recovery");
}
