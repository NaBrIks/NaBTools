// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {JSDOM} from 'jsdom';
import {encode} from '../../web/cbor.js';
import {devicePage} from '../../web/devices.js';
import {observationTable} from '../../web/observations.js';
import {scanModeControl} from '../../web/scan-mode.js';
function page(){const dom=new JSDOM(readFileSync(new URL('../../web/index.html',import.meta.url),'utf8'));globalThis.document=dom.window.document;
 const $=id=>document.getElementById(id),node=(tag,text,cls)=>{const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n};return {dom,$,node,t:k=>k,details:()=>{},command:()=>{}};
}
const device={slot:0,generation:1,address:'01:02:03:04:05:06',protocol:'Test profile',packets:1,commands:[{key:'test',label:'Do',status:'ok'}],events:[]};
const message=n=>({slot:0,generation:1,ordinal:n,time_ms:n,sequence:n,schema:'test.v1',payload:encode({value:n,unchanged:'text'})});
test('live device/CBOR updates preserve nodes, focus and selected text',()=>{
 const p=page(),render=devicePage(p),snapshot={boot_id:1,runtime:{source:'test',devices:[device]},messages:[message(1)]};render(snapshot);
 const row=p.$('devices').tBodies[0].children[1],button=row.querySelector('button'),valueRow=p.$('values').tBodies[0].firstChild;
 button.focus();render({...snapshot,runtime:{...snapshot.runtime,devices:[{...device,packets:2}]},messages:[...snapshot.messages,message(2)]});
 assert.equal(document.activeElement,button);assert.equal(row,p.$('devices').tBodies[0].children[1]);assert.equal(valueRow,p.$('values').tBodies[0].firstChild);assert.equal(valueRow.children[1].textContent,'2');
 const range=document.createRange();range.selectNodeContents(valueRow.children[1]);const selection=document.getSelection();selection.removeAllRanges();selection.addRange(range);assert.equal(selection.toString(),'2');
 render({...snapshot,messages:[message(3)]});assert.equal(selection.toString(),'2');assert.equal(valueRow.children[1].textContent,'2');
 selection.removeAllRanges();render({...snapshot,messages:[message(3)]});assert.equal(valueRow.children[1].textContent,'3');
 const select=p.$('message-select'),option=select.firstChild;select.focus();render({...snapshot,messages:[message(4)]});assert.equal(option,select.firstChild);assert.equal(valueRow.children[1].textContent,'3');
 select.blur();render({...snapshot,messages:[message(4)]});assert.equal(valueRow.children[1].textContent,'4');
 render({...snapshot,messages:[message(3),message(4)]});select.focus();select.value='3';select.dispatchEvent(new p.dom.window.Event('change'));
 assert.equal(document.activeElement,select);assert.equal(valueRow.children[1].textContent,'3','explicit selection updates the viewer before blur');
 p.dom.window.close();
});
test('advertising rows survive updates, sorting waits during selection, and handlers remain clickable',()=>{
 const p=page();let added;const render=observationTable({...p,onAdded:()=>{},command:v=>added=v});
 const base={address:'01:02:03:04:05:06',address_type:1,sid:255,name:'Example',manufacturer:'Company',manufacturer_id:1,rssi:-40,packets:1,last_seen_ms:1,status:'complete',handlers:[{key:'test.driver',label:'Driver',evidence:'Test',decoded:true}]};
 const options={sortKey:'company',sortDirection:'ascending',language:'en',now:1};const runtime={source:'test',devices:[],observations:[base]};render(runtime,options);
 const row=p.$('observations').tBodies[0].firstChild,button=row.children[9].querySelector('button');
 render({...runtime,observations:[{...base,packets:2}]},options);assert.equal(row,p.$('observations').tBodies[0].firstChild);assert.equal(button,row.children[9].querySelector('button'));assert.equal(row.children[7].textContent,'2');
 const selection=document.getSelection(),range=document.createRange();range.selectNodeContents(row.children[0]);selection.addRange(range);
 render({...runtime,observations:[]},options);assert.equal(selection.toString(),'Company');assert.equal(row.parentNode,p.$('observations').tBodies[0]);
 selection.removeAllRanges();button.click();assert.equal(added.add_handler,'test.driver');assert.deepEqual(added.address,Uint8Array.of(1,2,3,4,5,6));
 render({...runtime,observations:[]},options);assert.match(p.$('observations').tBodies[0].textContent,/empty/);p.dom.window.close();
});
test('classification columns render and filter backend claims without device commands',()=>{
 const p=page(),render=observationTable({...p,onAdded:()=>{},command:()=>{throw Error('Passive classification must not issue commands')}});
 const row={address:'01:02:03:04:05:06',address_type:1,sid:255,name:'SmartRemote',manufacturer_id:null,packets:1,rssi:-81,last_seen_ms:0,status:'complete',
  classification:{type:'remote_control',strength:'hint',source:'local_name',value:0,capabilities:[{key:'hid',source:'service_uuid',value:0x1812}]}};
 const runtime={source:'test',sig:'OFF',devices:[],observations:[row]},options={sortKey:'kind',sortDirection:'ascending',language:'ru',now:1000};render(runtime,options);
 const tr=p.$('observations').tBodies[0].firstChild;assert.equal(tr.cells.length,11);assert.equal(tr.cells[0].textContent,'—');
 assert.equal(tr.cells[2].textContent,'Пульт ?');assert.equal(tr.cells[3].textContent,'Устройство ввода');assert.match(tr.cells[4].textContent,/Имя; UUID 0x1812/);
 p.$('filter').value='пульт';render(runtime,options);assert.equal(tr,p.$('observations').tBodies[0].firstChild);
 p.$('filter').value='несуществующий';render(runtime,options);assert.equal(p.$('observations').tBodies[0].firstChild.cells[0].colSpan,11);
 p.dom.window.close();
});
test('scan selector sends only scan_mode and follows backend application/pending state',()=>{
 const p=page(),select=p.$('scan-mode');let sent;const render=scanModeControl(select,(value,_,message)=>{sent={value,message}},p.t);
 render({scan_mode:'passive',requested_scan_mode:'passive',scan_mode_supported:true},false);
 assert.equal(select.disabled,false);select.value='active';select.dispatchEvent(new p.dom.window.Event('change'));
 assert.deepEqual(sent,{value:{scan_mode:'active'},message:'scan_mode_accepted'});
 render({scan_mode:'passive',requested_scan_mode:'active',scan_mode_pending:true,scan_mode_supported:true},false);
 assert.equal(select.value,'active');assert.equal(select.disabled,true);
 render({scan_mode:'active',requested_scan_mode:'active',scan_mode_supported:true},false);assert.equal(select.disabled,false);
 render({scan_mode:'active',scan_mode_supported:true},true);assert.equal(select.disabled,true);
 render({},false);assert.equal(select.disabled,true);p.dom.window.close();
});
