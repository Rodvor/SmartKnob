#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>

namespace knob {
constexpr int CONFIG_SIZE = 38;
constexpr float PI_F = 3.14159265359f;
// Wire/page indices: volume, media, brightness, Discord.
struct Page { uint8_t step, strength, buildup, notches; uint16_t arc; };
struct Shortcut { uint8_t modifiers, key; };
struct Config {
  uint16_t sleepMinutes = 10;
  uint8_t count = 4;
  uint8_t order[4] = {0, 1, 2, 3};
  Page pages[4] = {{2,50,60,30,270}, {5,40,50,20,270},
                   {4,40,40,20,270}, {5,60,150,3,270}};
  Shortcut shortcuts[3] = {{0,0x68}, {4,0x68}, {6,0x68}};
};
// Retain the v1 decoder so installed configurations migrate without losing
// order, shortcuts or strength. Only bounded pages use the percentage setting.
inline bool normaliseSteps(Config& c) {
  bool changed=false;
  for(int i: {0,2}) {
    uint8_t step=c.pages[i].step<=2?2:4;
    if(step!=c.pages[i].step) { c.pages[i].step=step; changed=true; }
  }
  return changed;
}
inline uint16_t u16(const uint8_t* b) { return b[0] | (uint16_t(b[1]) << 8); }
inline void put16(uint8_t* b, uint16_t v) { b[0]=v; b[1]=v>>8; }
inline bool valid(const Config& c) {
  if (c.sleepMinutes > 120 || c.count < 1 || c.count > 4) return false;
  unsigned seen=0;
  for(int i=0;i<4;i++) { if(c.order[i]>3 || (seen & (1<<c.order[i]))) return false; seen|=1<<c.order[i]; }
  for(int i=0;i<4;i++) {
    auto p=c.pages[i];
    if(p.step<1 || p.step>25 || 100%p.step || p.strength>80 || p.buildup<1 || p.buildup>200 ||
       p.notches<3 || p.notches>100 || p.arc<90 || p.arc>300) return false;
  }
  if(c.pages[3].notches != 3) return false;
  for(auto s:c.shortcuts) if(s.modifiers>15 || s.key<4 || s.key>0x73) return false;
  return true;
}
inline void encode(const Config& c, uint8_t* b) {
  b[0]=1; put16(b+1,c.sleepMinutes); b[3]=c.count;
  memcpy(b+4,c.order,4);
  for(int i=0;i<4;i++) { int j=8+i*6; auto p=c.pages[i]; b[j]=p.step; b[j+1]=p.strength;
    b[j+2]=p.buildup; b[j+3]=p.notches; put16(b+j+4,p.arc); }
  for(int i=0;i<3;i++) { b[32+2*i]=c.shortcuts[i].modifiers; b[33+2*i]=c.shortcuts[i].key; }
}
inline bool decode(const uint8_t* b, int size, Config& result) {
  if(size!=CONFIG_SIZE || b[0]!=1) return false;
  Config c; c.sleepMinutes=u16(b+1); c.count=b[3]; memcpy(c.order,b+4,4);
  for(int i=0;i<4;i++) { int j=8+i*6; c.pages[i]={b[j],b[j+1],b[j+2],b[j+3],u16(b+j+4)}; }
  for(int i=0;i<3;i++) c.shortcuts[i]={b[32+2*i],b[33+2*i]};
  if(!valid(c)) return false;
  result=c; return true;
}
inline uint32_t hash(const uint8_t* b, int n) {
  uint32_t h=2166136261u; for(int i=0;i<n;i++) h=(h^b[i])*16777619u; return h;
}
// State snapshots can race fast dial input. Keep link freshness separate from
// acknowledgement freshness so continuous turns do not disable the motor.
class HostValue {
public:
  float value=0;
  bool known=false;
  uint16_t pending=0;
  uint32_t stateAt=0, pendingAt=0;
  void reset() { known=false; pending=0; }
  void sent(uint16_t sequence,uint32_t now) { pending=sequence; pendingAt=now; }
  void receive(float actual,uint16_t ack,uint32_t now) {
    stateAt=now;
    if(!pending || pending==ack || uint32_t(now-pendingAt)>1000) {
      value=actual; known=true; pending=0;
    }
  }
  bool fresh(uint32_t now) const { return known && uint32_t(now-stateAt)<3000; }
};
// One staged transaction; incomplete, interleaved or stale writes never apply.
class ConfigTransfer {
public:
  uint8_t bytes[CONFIG_SIZE]{};
  void reset() { receiving=false; }
  void begin(uint8_t id, uint32_t now) { transaction=id; received=0; started=now; receiving=true; }
  bool append(uint8_t id, int offset, const uint8_t* data, int size) {
    if(!receiving || id!=transaction || offset!=received || size<1 || size>17 || received+size>CONFIG_SIZE) return false;
    memcpy(bytes+received,data,size); received+=size; return true;
  }
  bool commit(uint8_t id, uint32_t checksum, uint32_t now, Config& c) {
    bool ok=receiving && id==transaction && received==CONFIG_SIZE && uint32_t(now-started)<5000 && hash(bytes,CONFIG_SIZE)==checksum && decode(bytes,CONFIG_SIZE,c);
    receiving=false; return ok;
  }
private:
  bool receiving=false;
  uint8_t transaction=0;
  int received=0;
  uint32_t started=0;
};
// Unwrapped angle is essential: no modulo operation at either endpoint.
class BoundedDial {
public:
  float start=0, span=0, spacing=0;
  int index=0, intervals=100;
  void reset(float angle, float value, const Page& p) {
    span=p.arc*PI_F/180; intervals=100/p.step; spacing=span/intervals;
    // Choose the closest equivalent arc, centred at the top of the display.
    float base=-PI_F/2-span/2;
    float target=base+span*value;
    start=base+std::round((angle-target)/(2*PI_F))*2*PI_F;
    index=std::lround(value*intervals);
  }
  float target(float value) const { return start+span*value; }
  int update(float angle) {
    if(!std::isfinite(angle)) return 0;
    float p=(angle-start)/spacing;
    if(std::fabs(p-index)<0.65f) return 0;
    int next=int(std::lround(p)); if(next<0) next=0; if(next>intervals) next=intervals;
    int delta=next-index; index=next; return delta;
  }
  float torque(float angle, const Page& p) const {
    float limit=p.strength/100.0f;
    float error= angle<start ? angle-start : angle>start+span ? angle-start-span : angle-(start+index*spacing);
    float gain=(angle<start || angle>start+span) ? 12.0f : (p.buildup/100.0f)*2/spacing;
    float t=error*gain;
    // Polarity follows this device's existing working motor/encoder convention.
    return std::fmax(-limit,std::fmin(limit,t));
  }
};
}
