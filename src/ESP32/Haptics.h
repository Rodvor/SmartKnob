#pragma once
#include "KnobConfig.h"

namespace knob {
struct HapticCommand {
  uint32_t epoch=0;
  Page settings{2,50,60,30,270};
  int page=0;
  float value=0;
  int discordChoice=0;
  bool enabled=false;
};
struct HapticState {
  uint32_t epoch=0;
  float angle=0, torque=0, start=0, spacing=0;
  int choice=0, delta=0, intervals=50;
  bool ready=false;
};
// Pure control law; called for every encoder sample, never from the renderer.
// Positive torque reduces raw encoder angle on this particular device, matching
// its established polarity. Damping therefore has the same sign as velocity.
class Haptics {
public:
  HapticState state;
  void configure(const HapticCommand& c,float angle,uint32_t now) {
    command=c; state={}; state.epoch=c.epoch; state.angle=angle;
    previous=angle; velocity=0; started=now; settled=false; settlingAt=0;
    awayTravel=0; awaySince=0; movingAway=false;
    bounded=c.page==0 || c.page==2;
    limit=c.settings.strength/100.0f;
    if(bounded) {
      dial.reset(angle,c.value,c.settings);
      index=dial.index;
      if(limit==0) dial.start=angle-index*dial.spacing;
      // Use exactly the same centre for positioning and normal detent torque.
      // E.g. High has 48% and 52% detents, not a 50% detent.
      target=dial.start+index*dial.spacing;
      state.start=dial.start; state.spacing=dial.spacing; state.intervals=dial.intervals;
    } else {
      state.spacing=2*PI_F/c.settings.notches;
      float base=-PI_F/2+(c.page==3?c.discordChoice*2*PI_F/3:0);
      target=base+std::round((angle-base)/(2*PI_F))*2*PI_F;
      index=std::lround((target+PI_F/2)/state.spacing);
      if(limit==0) index=std::lround((angle+PI_F/2)/state.spacing);
    }
    entryDistance=std::fabs(angle-target);
    state.choice=choice();
    state.ready=limit==0 && c.enabled;
  }
  const HapticState& update(float angle,float dt,uint32_t now) {
    state.delta=0; state.torque=0;
    if(!std::isfinite(angle) || !std::isfinite(dt) || dt<=0) return state;
    state.angle=angle;
    if(dt>0.02f) { previous=angle; velocity=0; return state; }
    float raw=(angle-previous)/dt; previous=angle;
    velocity += dt/(0.008f+dt)*(raw-velocity);
    if(!command.enabled) return state;
    float error=0,gain=0,damping=0;
    if(!state.ready) {
      error=angle-target;
      if(std::fabs(error)<0.015f && std::fabs(velocity)<0.15f) {
        if(!settled) { settlingAt=now; settled=true; }
        if(uint32_t(now-settlingAt)>=100) state.ready=true;
      } else settled=false;
      // A deliberate turn opposing positioning hands control back to the hand.
      // Require sustained outward travel, not one noisy sample or a small overshoot.
      if(error*velocity>0 && std::fabs(velocity)>0.15f) {
        if(!movingAway) { movingAway=true; awaySince=now; awayTravel=0; }
        awayTravel+=std::fabs(velocity)*dt;
      } else { movingAway=false; awayTravel=0; }
      float takeoverDistance=std::fmax(0.04f,std::fmin(0.15f,state.spacing*0.5f));
      bool takeover=movingAway && uint32_t(now-awaySince)>=60 &&
        awayTravel>=takeoverDistance && std::fabs(error)>entryDistance+takeoverDistance;
      if(!state.ready && (takeover || uint32_t(now-started)>=2500)) {
        engageAt(angle); // no HID/BLE input for the handover itself
      }
      if(!state.ready) {
        gain=2.0f; damping=0.055f;
        state.torque=clamp(deadband(error)*gain+velocity*damping,std::fmin(limit,0.35f));
        return state;
      }
    }
    int old=index;
    if(bounded) { dial.update(angle); index=dial.index; }
    else {
      float position=(angle+PI_F/2)/state.spacing;
      if(std::fabs(position-index)>=0.65f) index=std::lround(position);
    }
    state.delta=index-old; state.choice=choice();
    float centre=bounded?dial.start+index*state.spacing:index*state.spacing-PI_F/2;
    error=angle-centre;
    bool outside=bounded && (angle<dial.start || angle>dial.start+dial.span);
    gain=outside?12.0f:(command.settings.buildup/100.0f)*limit*2/state.spacing;
    damping=outside?0.045f:0.025f;
    state.torque=clamp(deadband(error)*gain+velocity*damping,limit);
    return state;
  }
private:
  HapticCommand command;
  BoundedDial dial;
  bool bounded=false,settled=false,movingAway=false;
  float awayTravel=0,entryDistance=0;
  uint32_t awaySince=0;
  float previous=0,velocity=0,target=0,limit=0;
  int index=0;
  uint32_t started=0,settlingAt=0;
  void engageAt(float angle) {
    if(bounded) {
      index=std::lround((angle-dial.start)/dial.spacing);
      if(index<0) index=0;
      if(index>dial.intervals) index=dial.intervals;
      dial.index=index;
    } else index=std::lround((angle+PI_F/2)/state.spacing);
    state.ready=true; state.choice=choice();
  }
  int choice() const { int n=command.settings.notches; return bounded?index:((index%n)+n)%n; }
  static float clamp(float t,float cap) { return std::fmax(-cap,std::fmin(cap,t)); }
  static float deadband(float e) { return std::fabs(e)<0.002f?0:e-std::copysign(0.002f,e); }
};
}
