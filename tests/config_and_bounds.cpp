#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "../src/ESP32/KnobConfig.h"
int main() {
  knob::Config c, out;
  uint8_t wire[knob::CONFIG_SIZE]; knob::encode(c,wire);
  assert(knob::decode(wire,sizeof(wire),out));
  assert(out.sleepMinutes==10 && out.pages[0].step==2 && out.shortcuts[2].modifiers==6);
  assert(knob::hash(wire,sizeof(wire))==0x0E7C33F7u); // shared Swift fixture
  for(int i=0;i<knob::CONFIG_SIZE;i++) {
    assert(!knob::decode(wire,i,out));
  }
  auto invalid=c; invalid.order[1]=0; assert(!knob::valid(invalid));
  invalid=c; invalid.pages[0].step=3; assert(!knob::valid(invalid));
  invalid=c; invalid.pages[2].arc=360; assert(!knob::valid(invalid));
  invalid=c; invalid.pages[3].notches=4; assert(!knob::valid(invalid));
  invalid=c; invalid.pages[0].strength=81; assert(!knob::valid(invalid));
  auto legacy=c; legacy.pages[0].step=1; legacy.pages[2].step=5;
  legacy.order[0]=3; legacy.order[3]=0;
  assert(knob::normaliseSteps(legacy));
  assert(legacy.pages[0].step==2 && legacy.pages[2].step==4);
  assert(legacy.order[0]==3 && legacy.shortcuts[2].modifiers==6);
  assert(!knob::normaliseSteps(legacy));
  knob::HostValue state;
  state.receive(.3f,0,100); assert(state.fresh(200));
  state.sent(7,200); state.receive(.4f,6,500);
  assert(state.value==.3f && state.pending==7 && state.fresh(3200));
  state.receive(.5f,7,600); assert(state.value==.5f && state.pending==0);
  state.sent(8,700); state.receive(.4f,7,1800); // lost final event reconciles
  assert(state.value==.4f && state.pending==0);
  assert(!state.fresh(4800)); state.reset(); assert(!state.known);
  knob::ConfigTransfer transfer;
  auto appendAll=[&](int id) {
    assert(transfer.append(id,0,wire,17)); assert(transfer.append(id,17,wire+17,17));
    assert(transfer.append(id,34,wire+34,4));
  };
  transfer.begin(2,100);
  assert(!transfer.append(3,0,wire,17));
  assert(!transfer.append(2,1,wire,17));
  assert(!transfer.commit(2,knob::hash(wire,sizeof(wire)),200,out));
  transfer.begin(3,100); appendAll(3);
  assert(!transfer.commit(3,123,200,out));
  transfer.begin(4,100); appendAll(4);
  assert(!transfer.commit(4,knob::hash(wire,sizeof(wire)),5100,out));
  transfer.begin(5,100); appendAll(5);
  assert(transfer.commit(5,knob::hash(wire,sizeof(wire)),200,out));
  assert(!transfer.commit(5,knob::hash(wire,sizeof(wire)),201,out));
  transfer.begin(6,100); transfer.reset();
  assert(!transfer.append(6,0,wire,17));
  for(int step: {1,2,4,5,10,20,25}) {
    auto p=c.pages[0]; p.step=step;
    knob::BoundedDial dial; dial.reset(12.0f,0.5f,p);
    assert(dial.intervals==100/step);
    assert(std::fabs(dial.target(.5f)-12.0f)<=knob::PI_F);
    dial.update(dial.start-10); assert(dial.index==0);
    assert(dial.update(dial.start-20)==0); // no wrap at minimum
    assert(dial.torque(dial.start-1,p)<0);
    dial.update(dial.start+100); assert(dial.index==dial.intervals);
    assert(dial.update(dial.start+200)==0); // no wrap at maximum
    assert(dial.torque(dial.start+dial.span+1,p)>0);
    assert(std::fabs(dial.torque(dial.start+100,p))<=p.strength/100.0f);
    dial.reset(0,0,p);
    assert(dial.update(dial.start+dial.spacing*.64f)==0);
    assert(dial.update(dial.start+dial.spacing*.66f)==1);
    assert(dial.update(dial.start+dial.spacing*.5f)==0);
    assert(dial.update(dial.start+dial.spacing*.34f)==-1);
    assert(dial.update(NAN)==0);
  }
  std::puts("Configuration and bounded dial checks passed");
}
