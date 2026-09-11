#include "../src/p26_grip.h"
#include "../src/p20_ik.h"
#include <stdexcept>
#include <cstdio>
void Check(bool v){if(!v)throw std::runtime_error("P26 test failed");}
int main(){
 p26::GripGate g;
 Check(!g.Update(1,true,100)); // held on focus regain cannot draw
 Check(!g.Update(0,true,110));Check(g.Update(1,true,120));
 Check(g.Update(0,true,130)); // no continuous squeeze required
 Check(!g.Update(0,true,221));
 Check(!g.Update(1,false,240));Check(!g.Update(1,true,250)); // entering hip while held does not toggle
 Check(!g.Update(0,true,260));Check(g.Update(1,true,270));
 g.Reset();Check(!g.Update(1,true,280));
 Check(!g.Update(NAN,true,300));Check(!g.Update(1,true,310));
 p20::Skeleton s; p20::Parents p{};p[12]=9;p[13]=12;
 s[9].p={0,0,0};s[12].p={30,0,0};s[13].p={60,0,0};
 auto old=s;
 Check(p20::SolveLimb(s,p,9,12,13,{68,0,0},{0,1,0},1.15f));
 Check(p20::Length(s[13].p-p20::V{68,0,0})<.01f);
 Check(p20::Length(s[12].p-s[9].p)<=34.501f);
 Check(p20::Length(s[13].p-s[12].p)<=34.501f);
 s=old;Check(p20::SolveLimb(s,p,9,12,13,{200,0,0},{0,1,0},1.15f));Check(p20::Length(s[13].p)<69.01f);
 s=old;Check(p20::SolveLimb(s,p,9,12,13,{40,0,0},{0,1,0},1.15f));Check(std::fabs(p20::Length(s[12].p)-30)<.01f);
 Check(!p20::SolveLimb(s,p,9,12,13,{40,0,0},{0,1,0},NAN));
 puts("P26 grip rising edge, proximity, release latch, focus safety and bounded arm reach passed");
}
