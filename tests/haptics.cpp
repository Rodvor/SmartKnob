#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/ESP32/Haptics.h"

int main() {
  knob::HapticCommand cmd; cmd.epoch=1; cmd.enabled=true; cmd.value=.5f;
  knob::Haptics h;
  float target=-knob::PI_F/2;
  h.configure(cmd,target,0);
  for(unsigned t=1;t<=150;t++) h.update(target,.001f,t);
  assert(h.state.ready && h.state.torque==0 && h.state.choice==25);
  // Haptics continue at 1 kHz even if a renderer consumes no snapshots for 100 ms.
  float step=h.state.spacing;
  auto first=h.update(target+step*.2f,.001f,151).torque;
  auto reversed=h.update(target-step*.2f,.001f,152).torque;
  assert(first>0 && reversed<first);
  for(unsigned t=153;t<253;t++) h.update(target,.001f,t);
  assert(std::fabs(h.state.torque)<.01f);
  // Crossing is measured at control rate, without UI-driven detent updates.
  h.update(target+step*.7f,.001f,253);
  assert(h.state.delta==1 && h.state.choice==26);
  h.update(h.state.start-10,.001f,254); assert(h.state.choice==0 && h.state.torque<0);
  h.update(h.state.start+20,.001f,255); assert(h.state.choice==50 && h.state.torque>0);
  auto stale=h.update(target,.030f,285); assert(stale.torque==0);
  cmd.enabled=false; h.configure(cmd,target,300); assert(h.update(target+1,.001f,301).torque==0);
  cmd.enabled=true; cmd.settings.strength=0; h.configure(cmd,target+.3f,400);
  assert(h.state.ready && h.update(target+.3f,.001f,401).torque==0);
  // Damped centring on a simple signed-inertia plant; no display/BLE calls.
  cmd.settings.strength=50;
  h.configure(cmd,target+.4f,0);
  float angle=target+.4f, velocity=0;
  for(unsigned t=1;t<=2400;t++) {
    float torque=h.update(angle,.001f,t).torque;
    assert(std::isfinite(torque) && std::fabs(torque)<=.5f);
    velocity+=(-80*torque-2*velocity)*.001f;
    angle+=velocity*.001f;
  }
  assert(std::fabs(angle-target)<.02f && std::fabs(velocity)<.1f && h.state.ready);
  // A held shaft exits automatic positioning into ordinary detents, never a
  // permanent zero-torque state. The transition must not produce an input.
  for(int page=0;page<4;page++) {
    cmd.page=page; cmd.settings.notches=page==3?3:20;
    h.configure(cmd,target+.4f,0);
    auto timeout=h.update(target+.4f,.001f,2501);
    assert(timeout.ready && timeout.delta==0);
    float held=timeout.start+timeout.choice*timeout.spacing;
    if(page==1 || page==3) held=std::round((target+.4f+knob::PI_F/2)/timeout.spacing)*timeout.spacing-knob::PI_F/2;
    auto active=h.update(held+.03f,.001f,2502);
    assert(active.ready && std::fabs(active.torque)>0);
    h.update(held+timeout.spacing*.8f,.001f,2503);
    assert(h.state.delta!=0);
  }
  // Moving away while the page is still positioning takes over promptly.
  cmd.page=0; cmd.settings.step=2; cmd.value=.5f;
  h.configure(cmd,target+.1f,0);
  bool takenOver=false;
  for(unsigned t=1;t<300;t++) {
    auto state=h.update(target+.1f+t*.001f,.001f,t);
    if(state.ready) { assert(state.delta==0); takenOver=true; break; }
  }
  assert(takenOver);
  // At High, 50% centres on 52%'s notch, not the gap at the top.
  // Switching from positioning to detents must not introduce a torque step.
  cmd.settings.step=4;
  for(float value: {0.0f,.01f,.5f,.99f,1.0f}) {
    cmd.value=value;
    h.configure(cmd,target,0);
    float notch=h.state.start+h.state.choice*h.state.spacing;
    if(value==.5f) assert(h.state.choice==13 && std::fabs(notch-target)>0.05f);
    for(unsigned t=1;t<=200;t++) {
      auto state=h.update(notch,.001f,t);
      assert(state.delta==0);
    }
    assert(h.state.ready && std::fabs(h.state.torque)<.001f);
  }
  puts("PASS: independent haptics, damped settling, timeout/manual handover, grid-aligned centring and endpoints");
}
