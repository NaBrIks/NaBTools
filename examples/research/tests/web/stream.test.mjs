// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ChangeStore,ResearchSocket} from '../../web/live-socket.js';
import {encode,decode} from '../../web/cbor.js';
const runtime={schema:'nabtools.research.v1',source:'fake',now_ms:10};
const meta=(device_ids=[],observation_ids=[])=>['runtime',{runtime,device_ids,observation_ids,message_dropped:0,stream_dropped:0}];
const packet=(seq,changes,reset=false,boot_id=1)=>({v:1,boot_id,seq,reset,changes});
const device={slot:0,generation:1,address:'a',commands:[],events:[]};
const message=ordinal=>['message',{ordinal,slot:0,generation:1,time_ms:ordinal,schema:'arbitrary.device.v1',payload:encode({custom:ordinal})}];

test('native producer replaces an observation at capacity without forcing recovery',async()=>{
 const {readFile}=await import('node:fs/promises');const {decode:independent}=await import('cborg');
 const frames=independent(await readFile(new URL('../../../../local/native-build/research-0.cbor.updates',import.meta.url)));
 const store=new ChangeStore();for(const bytes of frames)store.receive(independent(bytes));
 assert.equal(store.state.runtime.observations.length,32);
 assert.ok(store.state.runtime.observations.some(row=>row.address==='01:02:03:04:05:46'));
 assert.equal(frames.map(bytes=>independent(bytes)).filter(p=>p.reset).length,1);
});
test('one reducer handles full state and live batches without profile knowledge',()=>{
 const s=new ChangeStore();s.receive(packet(4,[meta([[0,1]]),['device',device],message(1)],true));
 s.receive(packet(5,[message(2),['control',{request_id:9,result:'ok'}]]));
 assert.equal(s.state.runtime.devices.length,1);assert.equal(s.state.messages.length,2);assert.equal(s.state.controls[0].request_id,9);
 s.receive(packet(6,[meta()]));assert.equal(s.state.runtime.devices.length,0);
});
test('snapshot cursor replays a bounded suffix, ignores duplicates, detects gaps and reboot',()=>{
 const s=new ChangeStore();s.receive(packet(11,[message(11)]));s.receive(packet(12,[message(12)]));
 s.receive(packet(11,[meta(),message(11)],true));assert.deepEqual(s.state.messages.map(m=>m.ordinal),[11,12]);
 assert.equal(s.state.stream_seq,12);assert.equal(s.receive(packet(12,[message(12)])),false);
 assert.throws(()=>s.receive(packet(14,[])),/gap/);assert.throws(()=>s.receive(packet(13,[],false,2)),/restarted/);
 s.begin();s.receive(packet(0,[meta()],true,2));assert.equal(s.state.boot_id,2);assert.equal(s.state.messages.length,0);
});
test('failed batch is atomic and retained history stays bounded',()=>{
 const s=new ChangeStore();s.receive(packet(0,[meta()],true));
 assert.throws(()=>s.receive(packet(1,[message(1),['unknown',{}]])));assert.equal(s.state.messages.length,0);
 for(let n=1;n<=30;n++)s.receive(packet(n,[message(n)]));
 assert.equal(s.state.messages.length,16);assert.equal(s.state.messages[0].ordinal,15);
 s.begin();for(let n=0;n<128;n++)s.receive(packet(n,[]));assert.throws(()=>s.receive(packet(128,[])),/buffer/);
});
class Socket {
 static peers=[];readyState=1;sent=[];
 constructor(){Socket.peers.push(this)}send(bytes){this.sent.push(decode(bytes))}close(){this.readyState=3;this.onclose?.()}
 emit(value){this.onmessage({data:encode(value).buffer})}
}
test('default host timers keep their global receiver during watchdog and reconnect',t=>{
 const timers=[],cleared=[];
 const host=function(){assert.ok(this==null||this===globalThis,'timer invoked with a ResearchSocket receiver')};
 t.mock.method(globalThis,'setTimeout',function(fn){host.call(this);timers.push(fn);return timers.length});
 t.mock.method(globalThis,'clearTimeout',function(id){host.call(this);cleared.push(id)});
 const c=new ResearchSocket({url:'ws://test/ws',WebSocketImpl:Socket});
 c.start();assert.equal(timers.length,1);
 Socket.peers.at(-1).close();assert.equal(timers.length,2);
 timers[1]();assert.equal(c.status,'connecting');
 c.stop();assert.ok(cleared.length>=2);
});
test('WS bootstrap, commands, gap recovery and reconnect use no HTTP path',()=>{
 const timers=[];let state,ack;
 const c=new ResearchSocket({url:'ws://test/ws',WebSocketImpl:Socket,setTimer:fn=>(timers.push(fn),timers.length),clearTimer:()=>{},onState:s=>state=s,onAck:v=>ack=v});
 c.start();const ws=Socket.peers.at(-1);ws.emit({v:1,type:'hello',boot_id:1,seq:0});assert.equal(ws.sent[0].snapshot,true);
 ws.emit(packet(0,[meta()],true));c.send({scan:true,request_id:7});assert.equal(ws.sent.at(-1).boot_id,1);
 ws.emit({v:1,type:'ack',request_id:7,status:'queued'});assert.equal(ack.status,'queued');
 ws.emit(packet(1,[message(1)]));assert.equal(state.messages.length,1);
 ws.emit(packet(3,[]));assert.equal(ws.sent.at(-1).snapshot,true);assert.equal(c.status,'recovering');
 ws.emit(packet(3,[meta(),message(3)],true));assert.equal(c.status,'live');assert.equal(state.stream_seq,3);
 ws.close();assert.equal(c.status,'offline');assert.equal(c.store.syncing,true);c.stop();
});

test('native stream envelope is independently decodable and carries opaque owned CBOR',async()=>{
 const {readFile}=await import('node:fs/promises');const {decode:independent}=await import('cborg');
 const packet=independent(await readFile(new URL('../../../../local/native-build/stream.cbor',import.meta.url)));
 assert.equal(packet.v,1);assert.equal(packet.boot_id,123);assert.equal(packet.seq,1);assert.equal(packet.reset,false);
 const [kind,value]=packet.changes[0];assert.equal(kind,'message');assert.equal(value.schema,'arbitrary.v1');assert.equal(value.generation,3);
 assert.equal(independent(value.payload),66);
});
