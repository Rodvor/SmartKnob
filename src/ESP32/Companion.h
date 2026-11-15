#pragma once
#include <Preferences.h>
#include "KnobConfig.h"

// All commands fit ATT's minimum 20-byte payload. BLE callbacks only queue RAM.
class Companion : public NimBLECharacteristicCallbacks {
public:
  static constexpr const char* SERVICE="7b4e1000-8e5a-4d2b-a6b9-4d534b4e4f42";
  NimBLECharacteristic *command=nullptr, *events=nullptr, *configuration=nullptr;
  knob::Config config;
  bool changed=false;
  uint8_t capabilities=0;
  uint32_t heartbeat=0;
  knob::HostValue state[2];
  void load() {
    Preferences p;
    if(p.begin("smartknob",true)) {
      uint8_t b[knob::CONFIG_SIZE];
      if(p.getBytes("config",b,sizeof(b))==sizeof(b)) knob::decode(b,sizeof(b),config);
      p.end();
    }
    if(knob::normaliseSteps(config)) {
      uint8_t b[knob::CONFIG_SIZE]; knob::encode(config,b);
      if(p.begin("smartknob",false)) { p.putBytes("config",b,sizeof(b)); p.end(); }
    }
  }
  void begin(NimBLEServer* server) {
    queue=xQueueCreate(16,sizeof(Packet));
    auto* service=server->createService(SERVICE);
    command=service->createCharacteristic("7b4e1001-8e5a-4d2b-a6b9-4d534b4e4f42", NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
    events=service->createCharacteristic("7b4e1002-8e5a-4d2b-a6b9-4d534b4e4f42",NIMBLE_PROPERTY::NOTIFY);
    configuration=service->createCharacteristic("7b4e1003-8e5a-4d2b-a6b9-4d534b4e4f42",NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
    command->setCallbacks(this);
    publishConfig();
    service->start();
    NimBLEDevice::getAdvertising()->addServiceUUID(SERVICE);
  }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    if(!info.isEncrypted() || c!=command || !queue) return;
    auto data=c->getValue();
    if(data.size()<1 || data.size()>20) return;
    Packet p{}; p.size=data.size(); memcpy(p.bytes,data.data(),p.size);
    xQueueSend(queue,&p,0);
  }
  bool active() const { return heartbeat && uint32_t(millis()-heartbeat)<4000; }
  bool available(int page) const {
    int i=page==0?0:1;
    return active() && (capabilities & (page==0?1:2)) && state[i].fresh(millis());
  }
  bool awaitingValue(int page) const { return state[page==0?0:1].pending!=0; }
  float value(int page) const { return state[page==0?0:1].value; }
  uint32_t configHash() const { uint8_t b[knob::CONFIG_SIZE]; knob::encode(config,b); return knob::hash(b,sizeof(b)); }
  void resetLink() {
    heartbeat=0; capabilities=0; state[0].reset(); state[1].reset();
    transfer.reset(); if(queue) xQueueReset(queue);
  }
  void poll(NimBLEKeyboard& keyboard) {
    Packet p;
    while(queue && xQueueReceive(queue,&p,0)==pdTRUE) {
      auto* b=p.bytes;
      if(b[0]==1 && p.size==2) {
        if(!active()) { state[0].reset(); state[1].reset(); }
        heartbeat=millis(); capabilities=b[1]&7;
      }
      else if(b[0]==4 && p.size==1) {
        state[0].reset(); state[1].reset(); transfer.reset();
      }
      else if(b[0]==2 && p.size==6 && (b[1]==0 || b[1]==2) && knob::u16(b+2)<=10000) {
        int i=b[1]==0?0:1; uint16_t ack=knob::u16(b+4);
        state[i].receive(knob::u16(b+2)/10000.0f,ack,millis());
      } else if(b[0]==3 && p.size==2 && active()) {
        if(b[1]==0) keyboard.playPause(); else if(b[1]==1) keyboard.nextTrack(); else if(b[1]==2) keyboard.prevTrack();
      } else if(b[0]==10 && p.size==2) {
        transfer.begin(b[1],millis());
      } else if(b[0]==11 && p.size>=4) {
        transfer.append(b[1],b[2],b+3,p.size-3);
      } else if(b[0]==12 && p.size==6) {
        uint32_t h=uint32_t(b[2]) | uint32_t(b[3])<<8 | uint32_t(b[4])<<16 | uint32_t(b[5])<<24;
        knob::Config next;
        bool ok=transfer.commit(b[1],h,millis(),next);
        if(ok) { knob::normaliseSteps(next); knob::encode(next,transfer.bytes); h=knob::hash(transfer.bytes,knob::CONFIG_SIZE); }
        if(ok && h!=configHash()) {
          Preferences prefs;
          ok=prefs.begin("smartknob",false);
          if(ok) { ok=prefs.putBytes("config",transfer.bytes,knob::CONFIG_SIZE)==knob::CONFIG_SIZE; prefs.end(); }
          if(ok) { config=next; changed=true; publishConfig(); }
        }
        uint8_t reply[3]={0x82,b[1],uint8_t(ok?0:1)}; events->notify(reply,sizeof(reply));
      }
    }
    keyboard.companionActive.store(active());
  }
  bool send(int page, int action, uint16_t value=0) {
    if(!active()) return false;
    if(++sequence==0) ++sequence;
    uint8_t b[7]={0x80,0,0,uint8_t(page),uint8_t(action),0,0};
    knob::put16(b+1,sequence); knob::put16(b+5,value);
    bool ok=events->notify(b,sizeof(b));
    if(ok && action==0 && (page==0 || page==2)) state[page==0?0:1].sent(sequence,millis());
    return ok;
  }
  void status(int page,bool hid,bool awake) {
    if(!active()) return;
    uint32_t h=configHash(); uint8_t b[9]={0x81,1,uint8_t(page),uint8_t(hid),uint8_t(awake),uint8_t(h),uint8_t(h>>8),uint8_t(h>>16),uint8_t(h>>24)};
    events->notify(b,sizeof(b));
  }
private:
  struct Packet { uint8_t size,bytes[20]; };
  QueueHandle_t queue=nullptr;
  uint16_t sequence=0;
  knob::ConfigTransfer transfer;
  void publishConfig() { uint8_t b[knob::CONFIG_SIZE]; knob::encode(config,b); configuration->setValue(b,sizeof(b)); }
};
