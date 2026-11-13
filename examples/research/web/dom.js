// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Keep live nodes (focus, selection and scroll) across incoming measurements.
export function setText(element,value){
 const text=String(value??'');if(element.textContent!==text)element.textContent=text;
}
export function selectedWithin(element){
 const selection=element.ownerDocument.getSelection?.();
 if(!selection||selection.isCollapsed||!selection.rangeCount)return false;
 try{return selection.getRangeAt(0).intersectsNode(element)}catch{return false}
}
export function keyedRows(parent){
 const rows=new Map();
 return (items,key,create,update)=>{
  // Do not move even a surviving row while the user is selecting/copying it.
  // The next live render applies the latest state after the selection clears.
  if(selectedWithin(parent))return false;
  const keep=new Set();let cursor=parent.firstChild;
  for(const item of items){const id=key(item);keep.add(id);let row=rows.get(id);
   if(!row){row=create(item);rows.set(id,row)}
   update(row,item);
   if(row!==cursor)parent.insertBefore(row,cursor);
   cursor=row.nextSibling;
  }
  for(const [id,row]of rows)if(!keep.has(id)){row.remove();rows.delete(id)}
  return true;
 };
}
