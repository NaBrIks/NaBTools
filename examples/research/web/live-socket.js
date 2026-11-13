// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {decode,encode} from './cbor.js';
const observationKey=row=>`${row.address}/${row.address_type}/${row.sid}`;
const deviceKey=row=>`${row.slot}/${row.generation}`;
const upsert=(rows,value,key,limit)=>{
 const id=key(value),index=rows.findIndex(row=>key(row)===id);
 if(index>=0)rows[index]=value;else{if(rows.length>=limit)throw Error('Collection limit');rows.push(value)}
};
const validSequence=value=>Number.isSafeInteger(value)&&value>=0;
export function applyChanges(state,changes){
 if(!Array.isArray(changes)||changes.length>105)throw Error('Invalid changes');
 for(const entry of changes){
  if(!Array.isArray(entry)||entry.length!==2)throw Error('Invalid change');
  const [kind,value]=entry;
  if(kind==='runtime'){
   if(value.runtime?.schema!=='nabtools.research.v1'||!Array.isArray(value.device_ids)||!Array.isArray(value.observation_ids))throw Error('Invalid runtime');
   Object.assign(state.runtime,value.runtime);
   const devices=new Set(value.device_ids.map(id=>id.join('/'))),observations=new Set(value.observation_ids.map(id=>id.join('/')));
   state.runtime.devices=state.runtime.devices.filter(row=>devices.has(deviceKey(row)));
   state.runtime.observations=state.runtime.observations.filter(row=>observations.has(observationKey(row)));
   state.message_dropped=value.message_dropped;state.stream_dropped=value.stream_dropped;
  }else if(kind==='device'){
   if(!Number.isInteger(value.slot)||value.slot<0||value.slot>=32||!validSequence(value.generation)||!value.generation)throw Error('Invalid device');
   upsert(state.runtime.devices,value,deviceKey,32);
  }else if(kind==='observation'){
   if(typeof value.address!=='string'||!Number.isInteger(value.address_type)||!Number.isInteger(value.sid))throw Error('Invalid observation');
   upsert(state.runtime.observations,value,observationKey,32);
  }else if(kind==='message'){
   if(!validSequence(value.ordinal)||!value.ordinal||!(value.payload instanceof Uint8Array))throw Error('Invalid message');
   upsert(state.messages,value,m=>m.ordinal,17);state.messages.sort((a,b)=>a.ordinal-b.ordinal);state.messages=state.messages.slice(-16);
  }else if(kind==='control'){
   upsert(state.controls,value,c=>c.request_id,9);state.controls=state.controls.slice(-8);
  }else throw Error('Unknown change '+kind);
 }
 return state;
}
export class ChangeStore {
 state=null;buffer=[];syncing=true;
 begin(){this.syncing=true;this.buffer=[]}
 receive(packet){
  if(packet.v!==1||!validSequence(packet.boot_id)||!packet.boot_id||!validSequence(packet.seq)||typeof packet.reset!=='boolean')throw Error('Invalid change envelope');
  if(packet.reset){
   // The full state uses exactly the same reducer as a live delta. Discard
   // queued events covered by its cursor, then replay only the later suffix.
   const next={boot_id:packet.boot_id,stream_seq:packet.seq,runtime:{devices:[],observations:[]},messages:[],controls:[]};
   applyChanges(next,packet.changes);this.state=next;this.syncing=false;
   const buffered=this.buffer;this.buffer=[];
   for(const p of buffered)if(p.boot_id!==next.boot_id||p.seq>next.stream_seq)this.receive(p);
   return true;
  }
  if(this.syncing){if(this.buffer.length===128)throw Error('Recovery buffer full');this.buffer.push(packet);return false}
  if(packet.boot_id!==this.state.boot_id)throw Error('Device restarted');
  if(packet.seq<=this.state.stream_seq)return false;
  if(packet.seq!==this.state.stream_seq+1)throw Error('Stream gap');
  const next={...this.state,runtime:{...this.state.runtime,devices:[...this.state.runtime.devices],observations:[...this.state.runtime.observations]},messages:[...this.state.messages],controls:[...this.state.controls]};
  applyChanges(next,packet.changes);next.stream_seq=packet.seq;this.state=next;return true;
 }
}
export class ResearchSocket {
 constructor({url,WebSocketImpl=WebSocket,onState=()=>{},onAck=()=>{},onStatus=()=>{},setTimer=(fn,delay)=>setTimeout(fn,delay),clearTimer=id=>clearTimeout(id),now=Date.now}){
  Object.assign(this,{url,WebSocketImpl,onState,onAck,onStatus,setTimer,clearTimer,now});this.store=new ChangeStore();this.status='offline';this.closed=true;
 }
 setStatus(value){this.status=value;this.onStatus(value)}
 start(){this.closed=false;this.connect();this.watch()}
 connect(){
  if(this.closed)return;
  this.store.begin();this.syncStarted=0;this.setStatus('connecting');this.lastSeen=this.now();
  const socket=this.socket=new this.WebSocketImpl(this.url);socket.binaryType='arraybuffer';
  socket.onmessage=event=>{
   if(this.socket!==socket)return;
   try{
    const bytes=event.data instanceof Uint8Array?event.data:new Uint8Array(event.data);
    if(bytes.length>32768)throw Error('Frame limit');
    const packet=decode(bytes);this.lastSeen=this.now();
    if(packet.type==='hello'){if(packet.v!==1)throw Error('Unsupported stream');this.recover();return}
    if(packet.type==='ack'){this.onAck(packet);return}
    const changed=this.store.receive(packet);
    if(changed){this.attempt=0;this.setStatus('live');this.onState(this.store.state,packet.reset||packet.changes.some(([kind])=>kind==='runtime'))}
   }catch(error){this.fail(error)}
  };
  socket.onerror=()=>{};
  socket.onclose=()=>{if(this.socket!==socket)return;this.setStatus('offline');this.store.begin();this.retry()};
 }
 recover(){
  if(this.socket?.readyState!==1)return;
  this.store.begin();this.syncStarted=this.now();this.setStatus('recovering');this.socket.send(encode({snapshot:true}));
 }
 fail(error){
  this.onStatus('recovering',String(error));
  if(this.store.syncing){this.socket?.close();return}
  this.recover();
 }
 retry(){
  if(this.closed)return;
  this.clearTimer(this.retryTimer);const delay=Math.min(5000,500*2**Math.min(4,this.attempt||0));this.attempt=(this.attempt||0)+1;
  this.retryTimer=this.setTimer(()=>this.connect(),delay);
 }
 watch(){
  if(this.closed)return;
  const limit=this.store.syncing?20000:8000;
  if(this.socket&&this.now()-(this.store.syncing?this.syncStarted||this.lastSeen:this.lastSeen)>limit)this.socket.close();
  this.watchTimer=this.setTimer(()=>this.watch(),1000);
 }
 send(value){
  if(this.status!=='live'||this.socket?.readyState!==1)throw Error('Wait for WebSocket state');
  this.socket.send(encode({...value,boot_id:this.store.state.boot_id}));
 }
 stop(){this.closed=true;this.clearTimer(this.watchTimer);this.clearTimer(this.retryTimer);const s=this.socket;this.socket=null;s?.close()}
}
