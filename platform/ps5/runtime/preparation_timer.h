#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
namespace apricot {
struct PreparationTime {unsigned elapsed=0,remaining=0;bool estimated=false,ready=false;};
// Estimate for the CURRENT edition. The18-trial hint is calibrated from both
// verified German profiles, never a completion condition for the compiler.
class PreparationTimer {
 double begin=0,run_begin=0,cycle_seconds=0,media_begin=0;
 unsigned cycles=0,last_state=0,expected_trials=18;bool started=false,run=false,media=false;
public:
 void reset(double now){*this={};if(std::isfinite(now)){begin=now;started=true;}}
 PreparationTime update(double now,const char*phase,unsigned state,unsigned long long frame){
  PreparationTime out;if(!std::isfinite(now))return out;if(!started)reset(now);if(now<begin||!phase)return out;
  out.elapsed=unsigned(std::min(4294967295.,std::floor(now-begin)));
  if(!strcmp(phase,"READY")){out.estimated=out.ready=true;return out;}
  if(!strcmp(phase,"validate")){
   if(!run){run=true;run_begin=now;last_state=state;}
   else if(state!=last_state){if(state>last_state&&now>run_begin){cycle_seconds+=now-run_begin;cycles+=state-last_state;}run_begin=now;last_state=state;}
   if(state>=expected_trials)expected_trials=state+2;
   if(cycles){double average=cycle_seconds/cycles;
    // Include measured compile/boot overhead in each cycle; reserve an extra
    // cycle for the longer final validation and title-media preparation.
    double seconds=std::max(1.,average*(expected_trials-state+1)-(now-run_begin));
    out.remaining=unsigned(std::min(4294967295.,std::ceil(seconds)));out.estimated=true;
   }
  }else if(!strcmp(phase,"compile-state")&&cycles){
   out.remaining=unsigned(std::min(4294967295.,std::ceil(std::max(1.,cycle_seconds/cycles*(expected_trials>state?expected_trials-state+2:2)))));out.estimated=true;
  }else if(!strcmp(phase,"TITLE IMAGE AND MUSIC")){
   if(!media){media=true;media_begin=now;}
   if(frame>=240&&frame<7800&&now>media_begin){out.remaining=unsigned(std::min(4294967295.,std::ceil((now-media_begin)*(7800-frame)/frame)));out.estimated=true;}
  }
  // Discovery, initial compilation and final disk commit have no reliable
  // rate yet. Keep elapsed time visible; never show a made-up zero countdown.
  return out;
 }
};
}
