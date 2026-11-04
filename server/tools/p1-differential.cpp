// Offline P1 adapter: production public APIs only; no client authority or room/network edits.
#include "expedition/run.hpp"
#include "expedition/encounter.hpp"
#include "expedition/build.hpp"
#include "sim/world.hpp"
#include "combat/weapon.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <vector>
#include <cstring>
#include <iomanip>
using namespace ac::expedition;
struct Record { std::string text; uint32_t u(const char* key) const { std::smatch m; return std::regex_search(text,m,std::regex(std::string("\"")+key+"\":([0-9]+)"))?static_cast<uint32_t>(std::stoull(m[1])):0u; } int n(const char* key) const { std::smatch m; return std::regex_search(text,m,std::regex(std::string("\"")+key+"\":([0-9]+)"))?static_cast<int>(std::stoll(m[1])):0; } std::string s(const char* key)const {std::smatch m; return std::regex_search(text,m,std::regex(std::string("\"")+key+"\":\"([^\"]+)\""))?m[1].str():"";} };
std::string bits(double x){uint64_t u;std::memcpy(&u,&x,8);std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<u;return s.str();}
uint64_t hash=0xcbf29ce484222325ull;
void emit(const std::string& s){for(unsigned char c:s){hash^=c;hash*=0x100000001b3ull;}std::cout<<s<<"|hash="<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<std::dec<<'\n';}
int main(int argc,char** argv){if(argc!=2)return 2;std::ifstream f(argv[1]);if(!f)return 2;std::vector<Record> cases,req,events;std::string line;while(std::getline(f,line)){Record r{line};if(!r.s("kind").empty())cases.push_back(r);else if(line.find("\"offer\"")!=std::string::npos)req.push_back(r);else if(line.find("\"head\"")!=std::string::npos)events.push_back(r);}
for(const auto& c:cases){auto name=c.s("name"),kind=c.s("kind");hash=0xcbf29ce484222325ull;RunState run;EncounterState enc;uint16_t id=static_cast<uint16_t>(c.n("pid"));beginRun(run,static_cast<uint32_t>(c.n("runId")),c.n("seed"),&id,1);beginEncounter(enc);
ac::sim::Entity p[7]{},target[7]{};for(int k=0;k<7;++k){p[k].active=true;p[k].id=1;p[k].hp=50;p[k].maxHp=100;ac::combat::createWeaponState(p[k].weapon);p[k].weapon.magInSlot[0]=1;p[k].weapon.reloadEndsAtMs=9999;enableBuild(p[k].build,name!="classic-neutral");if(k<6)selectCard(p[k].build,static_cast<uint8_t>(k+1));else for(uint8_t card=1;card<=6;++card)selectCard(p[k].build,card);target[k].active=true;target[k].id=2;target[k].kind=ac::sim::EntityKind::kSheep;target[k].hp=100;}
for(int t=0;t<c.n("ticks");++t){std::ostringstream s;s<<"case="<<name<<"|tick="<<t;
if(kind=="run"){int ack=0;for(const auto& r:req)if(r.n("tick")==t)ack=request(run,id,r.n("runId")?static_cast<uint32_t>(r.n("runId")):run.runId,r.n("offer"),r.u("id"),static_cast<uint8_t>(r.n("op")),static_cast<uint8_t>(r.n("choice")));auto before=run.stage;tickRun(run,50);if(run.stage==Stage::kCombat){if(before!=Stage::kCombat)beginEncounter(enc);stepEncounter(enc,50,c.n("defended")!=0,static_cast<uint32_t>(c.n("invaders")));run.objectiveHp=static_cast<uint16_t>(enc.hpMilli/1000);run.progressMs=enc.progressMs;run.combatLeftMs=enc.remainingMs;if(enc.result!=EncounterResult::kNone)finishEncounter(run,static_cast<Result>(enc.result));}const auto& p0=run.players[0];s<<"|stage="<<int(run.stage)<<"|node="<<int(run.node)<<"|result="<<int(run.result)<<"|clock="<<run.clockMs<<"|revision="<<run.revision<<"|hp="<<run.objectiveHp<<"|progress="<<run.progressMs<<"|left="<<run.combatLeftMs<<"|deadline="<<run.rewardDeadlineMs<<"|rng="<<run.rewardRng.a<<"|nextOffer="<<run.nextOfferId<<"|mask="<<int(p0.cardMask)<<"|offer="<<p0.offerId<<"|choices="<<int(p0.choices[0])<<','<<int(p0.choices[1])<<','<<int(p0.choices[2])<<"|resolved="<<p0.resolved<<"|rerolls="<<int(p0.rerollsLeft)<<"|lastId="<<p0.lastRequestId<<"|lastResult="<<int(p0.lastResult)<<"|supply="<<p0.supplyPending<<"|ack="<<ack;
}else if(kind=="encounter"){uint32_t inv=static_cast<uint32_t>(c.n("invaders"));if(c.n("spikeInvaders")>0&&t==c.n("spikeTick"))inv=static_cast<uint32_t>(c.n("spikeInvaders"));stepEncounter(enc,50,c.n("defended")!=0,inv);s<<"|hpMilli="<<enc.hpMilli<<"|progress="<<enc.progressMs<<"|left="<<enc.remainingMs<<"|result="<<int(enc.result);
}else{for(int k=0;k<7;++k){auto& a=p[k];auto& b=target[k];a.vel.x=0;for(const auto& e:events)if(e.n("tick")==t)a.vel.x=e.n("moving");updateBuild(a,static_cast<uint32_t>(t*50),50);double mod=1;for(const auto& e:events)if(e.n("tick")==t){auto part=e.n("head")?ac::config::HitPart::kHead:ac::config::HitPart::kTorso;mod=calculateHitModifier(a,b,part,e.n("distance"),static_cast<uint32_t>(t*50));onHit(a,b,part,e.n("distance"),static_cast<uint32_t>(t*50));if(e.n("kill")){b.hp=0;onKill(a,b,e.n("distance"),static_cast<uint32_t>(t*50));b.hp=100;}}auto& st=a.build;s<<"|b"<<k<<"="<<int(st.selectedMask)<<','<<st.stationaryMs<<','<<bits(mod)<<','<<bits(a.hp)<<','<<a.weapon.magInSlot[0]<<','<<a.weapon.reserveAmmo<<','<<bits(a.weapon.reloadEndsAtMs)<<','<<st.triggerCount<<','<<st.refundReadyAtMs<<','<<st.healReadyAtMs<<','<<st.tempoReadyAtMs<<','<<b.build.targets[0].markUntilMs<<','<<b.build.targets[0].staggerReadyAtMs<<','<<bits(b.ai.timerMs)<<','<<int(b.state);}}
emit(s.str());}}
return 0;}
