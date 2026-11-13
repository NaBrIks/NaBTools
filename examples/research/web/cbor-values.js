// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Generic data presentation. No protocol, field-name or device-type branches.
export const hex=bytes=>Array.from(bytes,n=>n.toString(16).padStart(2,'0')).join(' ');
export function fields(value,path='',depth=0){
 if(depth>24)throw Error('CBOR value depth limit');
 const type=value===null?'null':value instanceof Uint8Array?'bytes':Array.isArray(value)?'array':typeof value;
 if(type==='object'||type==='array'){
  const entries=Object.entries(value);if(!entries.length)return [{path:path||'/',type,value:type==='array'?'[]':'{}'}];
  return entries.flatMap(([key,v])=>fields(v,path+'/'+key.replaceAll('~','~0').replaceAll('/','~1'),depth+1));
 }
 return [{path:path||'/',type:type==='number'&&Number.isInteger(value)?'integer':type,
          value:type==='bytes'?hex(value):type==='string'?value:String(value)}];
}
