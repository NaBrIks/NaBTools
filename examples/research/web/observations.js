// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {keyedRows,setText} from './dom.js';
import {sortObservations} from './observation-sort.js';
import {describeClassification} from './classification.js';
export function observationTable({$,node,t,details,command,onAdded}){
 const body=$('observations').tBodies[0],sync=keyedRows(body);
 return (runtime,{sortKey,sortDirection,language,now})=>{
  const fixture=runtime.source==='native fixture',filter=$('filter').value.trim().toLowerCase();
  const company=row=>row.manufacturer||(row.manufacturer_id===null?'—':(language==='ru'?'Нет в ':'Not in ')+runtime.sig+' · 0x'+row.manufacturer_id.toString(16));
  const rows=sortObservations(runtime.observations,sortKey,sortDirection,language,now,company).filter(row=>{
   const c=describeClassification(row,language);return !filter||`${row.name} ${row.address} ${company(row)} ${c.type} ${c.capabilities} ${c.basis} ${(row.handlers||[]).map(h=>h.label).join(' ')}`.toLowerCase().includes(filter)});
  sync(rows.length?rows:[null],row=>row?`${row.address}/${row.address_type}/${row.sid}`:'empty',()=>{
   const tr=node('tr');tr.append(...Array.from({length:11},()=>node('td')));
   const c=tr.children;c[0].className='company-name';c[1].className='device-name';c[2].className='device-kind';c[3].className='capabilities';c[4].className='classification-basis';c[5].className='mac';
   for(const i of [6,7,8])c[i].className='number';c[9].className='handler-name';
   tr.handlers=keyedRows(c[9]);const button=node('button');button.onclick=()=>details(tr.data.address,tr.data);c[10].append(button);return tr;
  },(tr,row)=>{
   tr.data=row;const c=tr.children;
   if(!row){c[0].colSpan=11;setText(c[0],t('empty'));for(let i=1;i<11;++i)c[i].hidden=true;return}
   tr.className=row.handlers?.length?'recognized':'';
   const knowledge=describeClassification(row,language);
   for(const [i,value]of [[0,company(row)],[1,row.name||'—'],[2,knowledge.type],[3,knowledge.capabilities],[4,knowledge.basis],[5,row.address],[6,row.rssi],[7,row.packets],[8,Math.max(0,(now-row.last_seen_ms)/1000).toFixed(1)]])setText(c[i],value);
   for(const i of [2,3,4])c[i].dataset.tooltip=knowledge.tooltip;
   c[0].dataset.tooltip=company(row);c[1].dataset.tooltip=row.name||row.address;
   tr.handlers(row.handlers?.length?row.handlers:[null],h=>h?.key||'none',()=>{
    const group=node('span',undefined,'handler-choice'),label=node('span'),add=node('button');group.append(label,add);
    add.onclick=()=>{const h=group.handler,r=tr.data;command({add_handler:h.key,address:Uint8Array.from(r.address.split(':').map(b=>parseInt(b,16))),address_type:r.address_type,sid:r.sid},onAdded)};return group;
   },(group,h)=>{group.handler=h;const [label,add]=group.children;setText(label,h?.label||'—');add.hidden=!h;if(!h)return;
    label.dataset.tooltip=h.evidence+(h.decoded?'':' · raw');
    const exists=runtime.devices.some(d=>d.address===row.address&&d.address_type===row.address_type&&d.protocol===h.label);
    setText(add,exists?t('added'):t('add'));add.disabled=fixture||exists;
   });
   setText(c[10].firstChild,row.status);c[10].firstChild.className=row.status==='complete'?'':'warn';
  });
 };
}
