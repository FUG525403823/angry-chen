import dgram from "node:dgram";
import fs from "node:fs";
const args = Object.fromEntries(process.argv.slice(2).map(x => { const i=x.indexOf("="); return [x.slice(0,i),x.slice(i+1)]; }));
const port=Number(args.port), target=Number(args.target), delay=Number(args.delay ?? 0), loss=Number(args.loss ?? 0);
if (![port,target].every(x=>Number.isInteger(x)&&x>0&&x<65536) || delay<0 || loss<0 || loss>100 || !args.report) throw Error("invalid proxy arguments");
const front=dgram.createSocket("udp4"), routes=new Map();
const stats={received:0,forwarded:0,dropped:0,duplicated:0,reordered:0,errors:0,delayMs:delay,lossPct:loss,peers:0};
const timers=new Set(); let stopping=false;
let blackoutAt=0;
const outageMs=Number(args.outage ?? 0);
if (!Number.isFinite(delay) || !Number.isFinite(loss) || !Number.isFinite(outageMs) || outageMs<0 || (outageMs>0 && (!args.trigger || ![2,4].includes(Number(args.team))))) throw Error("invalid impairment profile");
function blackout(){
 if(!outageMs)return false;
 if(!blackoutAt && Array.from({length:Number(args.team)},(_,i)=>`${args.trigger}/peer-${i+1}.json.ready`).every(p=>fs.existsSync(p))){blackoutAt=Date.now();stats.blackoutStarted=true;}
 if(!blackoutAt)return false;
 if(Date.now()-blackoutAt<outageMs){stats.blackoutDropped=(stats.blackoutDropped??0)+1;return true;}
 stats.blackoutRecovered=true;return false;
}
function forward(socket, packet, port, address) {
 stats.received++; const n=stats.received;
 if(blackout()){stats.dropped++;return;}
 if ((n*37)%100 < loss) { stats.dropped++; return; }
 const reorder=args.reorder==="1" && n%11===0; if(reorder) stats.reordered++;
 const send=(extra)=>{const timer=setTimeout(()=>{timers.delete(timer); if(!stopping) socket.send(packet,port,address,e=>{if(e)stats.errors++;else stats.forwarded++;});},delay+(reorder?delay+25:0)+extra);timers.add(timer);};
 send(0); if(args.duplicate==="1" && n%13===0){stats.duplicated++;send(3);}
}
function report(){fs.writeFileSync(args.report,JSON.stringify(stats,null,2));}
front.on("error",e=>{console.error(e.message);process.exitCode=1;stop();});
front.on("message",(packet,peer)=>{
 const key=peer.address+":"+peer.port; let route=routes.get(key);
 if(!route){route=dgram.createSocket("udp4");routes.set(key,route);stats.peers=routes.size;route.on("error",()=>stats.errors++);route.on("message",p=>forward(front,p,peer.port,peer.address));}
 forward(route,packet,target,"127.0.0.1");
});
function stop(){if(stopping)return;stopping=true;clearInterval(pulse);for(const t of timers)clearTimeout(t);report();for(const r of routes.values())r.close();front.close();}
const pulse=setInterval(report,500);
process.on("SIGTERM",stop);process.on("SIGINT",stop);
front.bind(port,"127.0.0.1",()=>{report();console.log("UDP IMPAIRMENT READY "+port);});
