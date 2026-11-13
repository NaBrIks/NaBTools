// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {encode} from './cbor.js';
import {keyedRows,setText,selectedWithin} from './dom.js';
export function interactionPanel({$,node,t,command}){
 const panel=$('device-interactions');
 const groups=['commands','events'].map(kind=>{const group=node('div',undefined,'interaction-group'),heading=node('h4'),empty=node('p',undefined,'muted'),table=node('table'),body=node('tbody');table.append(body);group.append(heading,empty,table);panel.append(group);return {kind,heading,empty,table,sync:keyedRows(body)}});
 return device=>{
  if(selectedWithin(panel))return;panel.hidden=!device;if(!device)return;
  for(const g of groups){const ops=device[g.kind]||[];setText(g.heading,t(g.kind==='commands'?'device_commands':'device_events'));setText(g.empty,t('not_declared'));g.empty.hidden=!!ops.length;g.table.hidden=!ops.length;
   g.sync(ops,op=>`${device.slot}/${device.generation}/${op.key}`,()=>{
    const row=node('tr'),label=node('td'),cell=node('td');row.append(label,cell);
    if(g.kind==='commands'){const button=node('button');button.onclick=()=>command({invoke_slot:row.device.slot,generation:row.device.generation,invoke:row.operation.key,arguments:encode({})},undefined,t('dispatched'));cell.append(button)}return row;
   },(row,op)=>{row.device=device;row.operation=op;setText(row.firstChild,op.label||op.key);row.firstChild.dataset.tooltip=op.key;const cell=row.children[1];
    if(g.kind==='commands'){setText(cell.firstChild,t('execute'));cell.firstChild.disabled=op.status!=='ok'}else setText(cell,op.status==='ok'?t('available'):t('unavailable'));
    cell.dataset.tooltip=op.reason||op.status;
   });
  }
 };
}
