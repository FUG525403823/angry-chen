import assert from "node:assert/strict";
import dgram from "node:dgram";
import {spawn} from "node:child_process";
import {mkdtemp,readFile,rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import {join} from "node:path";
import {fileURLToPath} from "node:url";
const bind=s=>new Promise((resolve,reject)=>{s.once("error",reject);s.bind(0,"127.0.0.1",()=>resolve(s.address().port));});
const receive=(s,accept=()=>true)=>new Promise((resolve,reject)=>{const timer=setTimeout(()=>{s.off("message",onMessage);reject(Error("UDP receive timeout"));},3000);function onMessage(p){if(!accept(p))return;clearTimeout(timer);s.off("message",onMessage);resolve(p);}s.on("message",onMessage);});
const root=await mkdtemp(join(tmpdir(),"ac-udp-test-"));
const echo=dgram.createSocket("udp4"), client=dgram.createSocket("udp4"), reserve=dgram.createSocket("udp4");
let child;
try{
 const target=await bind(echo);await bind(client);const port=await bind(reserve);await new Promise(r=>reserve.close(r));
 echo.on("message",(p,peer)=>echo.send(p,peer.port,peer.address));
 child=spawn(process.execPath,[fileURLToPath(new URL("udp-impairment.mjs",import.meta.url)),`port=${port}`,`target=${target}`,"delay=30","loss=0","reorder=1","duplicate=1","trace=1",`report=${join(root,"proxy.json")}`],{stdio:["ignore","pipe","pipe"]});
 await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error("proxy startup timeout")),3000);child.once("exit",code=>{clearTimeout(timer);reject(Error(`proxy exit ${code}`));});child.stdout.on("data",b=>{if(b.toString().includes("UDP IMPAIRMENT READY")){clearTimeout(timer);resolve();}});});
 const start=performance.now();const response=receive(client);client.send(Buffer.from("payload"),port,"127.0.0.1");assert.equal((await response).toString(),"payload");assert.ok(performance.now()-start>=50,"both directions must delay");
 for(let i=0;i<30;i++){const next=receive(client);client.send(Buffer.from("p"+i),port,"127.0.0.1");await next;}
 const packet=Buffer.alloc(24);packet[0]=1;packet[1]=4;packet.writeUInt16LE(1,2);packet.writeUInt16LE(7,4);packet.writeUInt32LE(123,8);packet.writeUInt32LE(100,12);packet.writeUInt32LE(15,16);packet.write("hide",20);
 const echoed=receive(client,p=>p.equals(packet));client.send(packet,port,"127.0.0.1");assert.deepEqual(await echoed,packet);
 const stats=await new Promise((resolve,reject)=>{const deadline=setTimeout(()=>{clearInterval(tick);reject(Error("proxy stats timeout"));},3000);const tick=setInterval(async()=>{try{const s=JSON.parse(await readFile(join(root,"proxy.json"),"utf8"));if(s.duplicated>0&&s.reordered>0&&s.forwarded>0&&JSON.parse(await readFile(join(root,"proxy.json.wire.json"),"utf8")).some(x=>x.direction==="down"&&x.msgId===123)){clearTimeout(deadline);clearInterval(tick);resolve(s);}}catch{}},100);});
 const trace=JSON.parse(await readFile(join(root,"proxy.json.wire.json"),"utf8"));
 assert.ok(trace.some(x=>x.direction==="up"&&x.session===7&&x.msgId===123&&x.ackBase===100&&x.ackBits===15));
 assert.ok(trace.some(x=>x.direction==="down"&&x.msgId===123));assert.ok(!JSON.stringify(trace).includes("hide"));assert.ok(trace.length<=4000);
 assert.equal(stats.errors,0);assert.equal(stats.peers,1);assert.equal(stats.dropped,0);console.log("PASS UDP proxy real roundtrip delay, payload, duplicates, reordering and peer mapping");
}finally{if(child&&child.exitCode===null){child.kill();await new Promise(r=>child.once("exit",r));}echo.close();client.close();await rm(root,{recursive:true,force:true});}
