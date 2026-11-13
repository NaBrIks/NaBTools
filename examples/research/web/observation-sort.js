// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {describeClassification} from './classification.js';
const columns={
 company:(r,now,company)=>company(r),
 device:r=>r.name||'',
 kind:(r,now,company,locale)=>describeClassification(r,locale).type,
 capabilities:(r,now,company,locale)=>describeClassification(r,locale).capabilities,
 basis:(r,now,company,locale)=>describeClassification(r,locale).basis,
 address:r=>r.address,
 rssi:r=>r.rssi,
 packets:r=>r.packets,
 age:(r,now)=>Math.max(0,now-r.last_seen_ms),
 handler:r=>(r.handlers||[]).map(h=>h.label).join(' · '),
 data:r=>r.status
};
export const observationSortKeys=Object.keys(columns);
export function sortObservations(rows,key,direction,locale,now,company){
 const value=columns[key]||columns.company,collator=new Intl.Collator(locale,{numeric:true,sensitivity:'base'});
 const compare=(a,b)=>typeof a==='number'&&typeof b==='number'?a-b:collator.compare(String(a),String(b));
 return [...rows].sort((a,b)=>{
  const av=value(a,now,company,locale),bv=value(b,now,company,locale),empty=v=>v===null||v===undefined||v==='';
  if(empty(av)!==empty(bv))return empty(av)?1:-1;
  return (direction==='descending'?-1:1)*compare(av??'',bv??'')||
   collator.compare(a.address,b.address)||(a.address_type-b.address_type)||(a.sid-b.sid);
 });
}
