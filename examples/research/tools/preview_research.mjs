// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {WebSocketServer} from 'ws';
import {decode,encode} from 'cborg';
const root=fileURLToPath(new URL('../../../',import.meta.url)),web=path.resolve(fileURLToPath(new URL('../web/',import.meta.url)));
const fixture=decode(await readFile(path.join(root,'local/native-build/research-1.cbor')));
const {devices,observations,...runtime}=fixture.runtime;
const meta=()=>['runtime',{runtime,device_ids:devices.map(d=>[d.slot,d.generation]),observation_ids:observations.map(r=>[r.address,r.address_type,r.sid]),message_dropped:0,stream_dropped:0}];
let seq=0;
const mime={'.html':'text/html; charset=utf-8','.css':'text/css','.js':'text/javascript','.woff':'font/woff','.txt':'text/plain'};
const server=createServer(async(req,res)=>{
 try{
  if(req.method!=='GET'){res.writeHead(405);res.end();return}
  const relative=new URL(req.url,'http://localhost').pathname;
  const target=path.resolve(web,relative==='/'?'index.html':decodeURIComponent(relative).slice(1));
  if(!target.startsWith(web+path.sep)){res.writeHead(403);res.end();return}
  const bytes=await readFile(target);res.writeHead(200,{'Content-Type':mime[path.extname(target)]||'application/octet-stream','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});res.end(bytes);
 }catch{res.writeHead(404);res.end()}
});
const wss=new WebSocketServer({server,path:'/ws',maxPayload:192});
wss.on('connection',socket=>{
 socket.send(encode({v:1,type:'hello',boot_id:1,seq}));
 socket.on('message',bytes=>{try{
  if(decode(bytes).snapshot===true)socket.send(encode({v:1,boot_id:1,seq,reset:true,changes:[meta(),...devices.map(d=>['device',d]),...observations.map(r=>['observation',r])]}));
  else socket.send(encode({v:1,type:'ack',request_id:decode(bytes).request_id||0,status:'unsupported',boot_id:1}));
 }catch{socket.close()}});
});
setInterval(()=>{const bytes=encode({v:1,boot_id:1,seq:++seq,reset:false,changes:[meta()]});for(const client of wss.clients)if(client.readyState===1)client.send(bytes)},1000);
const port=Number(process.env.NABTOOLS_PREVIEW_PORT||5279);
server.listen(port,'127.0.0.1',()=>console.log(`NaBTools fixture preview http://127.0.0.1:${port} (not hardware)`));
