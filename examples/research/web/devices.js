// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {decode} from './cbor.js';
import {fields} from './cbor-values.js';
import {interactionPanel} from './interactions.js';
import {keyedRows,setText,selectedWithin} from './dom.js';

export function devicePage({$,node,t,details,command}){
 const renderInteractions=interactionPanel({$,node,t,command});
 let selected='*',selectedOrdinal=null,follow=true,last,shownMessage,shownBoot;
 const key=d=>`${d.slot}:${d.generation}`;
 const deviceBody=$('devices').tBodies[0],valueBody=$('values').tBodies[0],select=$('message-select');
 const syncDevices=keyedRows(deviceBody),syncValues=keyedRows(valueBody),syncOptions=keyedRows(select);
 select.onchange=()=>{selectedOrdinal=Number(select.value);follow=false;$('follow').checked=false;render(last,true)};
 $('follow').onchange=()=>{follow=$('follow').checked;render(last)};
 // Resume from the current model when an open native selector finishes.
 select.onblur=()=>render(last);
 function render(snapshot,userChoice=false){
  if(!snapshot)return;
  if(last?.boot_id&&last.boot_id!==snapshot.boot_id){selected='*';selectedOrdinal=null;follow=true;shownMessage=null;}
  last=snapshot;
  const devices=snapshot.runtime.devices,messages=snapshot.messages||[],fixture=snapshot.runtime.source==='native fixture';
  syncDevices([null,...devices],d=>d?key(d):'*',d=>{
   const tr=node('tr'),title=node('td'),choose=node('button');title.append(choose);tr.append(title);
   choose.onclick=()=>{selected=tr.device?key(tr.device):'*';follow=true;render(last)};
   if(d){tr.append(node('td',undefined,'mac'),node('td',undefined,'number'),node('td',undefined,'number'));
    const actions=node('td'),remove=node('button','×');remove.onclick=()=>command({remove_slot:tr.device.slot,generation:tr.device.generation});actions.append(remove);tr.append(actions);
   }else title.colSpan=5;
   return tr;
  },(tr,d)=>{tr.device=d;tr.className=selected===(d?key(d):'*')?'selected':'';
   const choose=tr.firstChild.firstChild;setText(choose,d?.protocol||t('all_messages'));if(!d)return;
   choose.dataset.tooltip=d.protocol;setText(tr.children[1],d.address);setText(tr.children[2],d.packets);setText(tr.children[3],key(d));
   const remove=tr.children[4].firstChild;remove.setAttribute('aria-label',t('remove'));remove.dataset.tooltip=t('remove');remove.disabled=fixture;
  });
  $('devices-empty').hidden=devices.length>0;
  const currentDevice=devices.find(d=>key(d)===selected);
  setText($('selected-device-title'),currentDevice?currentDevice.protocol:selected==='*'?t('all_messages'):t('removed_device'));
  setText($('selected-device-info'),currentDevice?`${currentDevice.address} · ID ${key(currentDevice)}`:selected==='*'?t('select_device'):t('removed_device'));
  renderInteractions(currentDevice);
  const available=messages.filter(m=>selected==='*'||key(m)===selected);
  setText($('message-count'),`${available.length} · ${t('dropped')}: ${snapshot.message_dropped||0}`);
  // Keep the displayed packet and native dropdown stable during user interaction.
  const choosing=document.activeElement===select;
  if(selectedWithin(valueBody)||(choosing&&!userChoice))return;
  if(follow||!available.some(m=>m.ordinal===selectedOrdinal))selectedOrdinal=available.at(-1)?.ordinal;
  if(!choosing)syncOptions([...available].reverse(),m=>`${snapshot.boot_id}/${m.ordinal}`,()=>node('option'),(option,m)=>{
   setText(option,`#${m.ordinal} · ${m.slot}:${m.generation} · ${m.time_ms} ms · ${m.schema}`);option.value=String(m.ordinal);
  });
  const choice=String(selectedOrdinal||'');if(select.value!==choice)select.value=choice;$('follow').checked=follow;
  const current=available.find(m=>m.ordinal===selectedOrdinal);
  $('values-empty').hidden=!!current;$('raw-message').disabled=!current;
  if(current===shownMessage&&shownBoot===snapshot.boot_id)return;
  shownMessage=current;shownBoot=snapshot.boot_id;
  if(!current){syncValues([],f=>f.path,()=>node('tr'),()=>{});return;}
  try{
   const value=decode(current.payload);
   syncValues(fields(value),f=>f.path,()=>{const tr=node('tr');tr.append(node('td',undefined,'field-path'),node('td'),node('td',undefined,'muted'));return tr},(tr,f)=>{
    const [path,cell,type]=tr.children;setText(path,f.path);setText(cell,f.value);setText(type,f.type);
    cell.className='field-value'+(['integer','number'].includes(f.type)?' number':'');path.dataset.tooltip=f.path;cell.dataset.tooltip=f.value;
   });
   $('raw-message').onclick=()=>details(`${current.schema} · #${current.ordinal}`,{...current,decoded:value});
  }catch(error){syncValues([{path:'error',value:String(error)}],f=>f.path,()=>{const tr=node('tr'),cell=node('td',undefined,'error');cell.colSpan=3;tr.append(cell);return tr},(tr,f)=>setText(tr.firstChild,f.value));
   $('raw-message').onclick=()=>details(current.schema,current);
  }
 }
 render.select=device=>{selected=key(device);follow=true;render(last)};
 return render;
}
