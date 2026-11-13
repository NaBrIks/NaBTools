// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import test from 'node:test';
import assert from 'node:assert/strict';
import {describeClassification} from '../../web/classification.js';
import {sortObservations,observationSortKeys} from '../../web/observation-sort.js';
test('advertising claims keep name uncertainty, requests and conflicts visible',()=>{
 const row={classification:{type:'remote_control',strength:'hint',source:'local_name',value:0,
  capabilities:[{key:'hid',source:'service_uuid',value:0x1812}],requested:[{key:'heart_rate',source:'solicitation',value:0x180d}]}};
 const ru=describeClassification(row,'ru');assert.equal(ru.type,'Пульт ?');assert.equal(ru.capabilities,'Устройство ввода');
 assert.match(ru.basis,/Имя/);assert.match(ru.basis,/UUID 0x1812/);assert.match(ru.basis,/Запрашивает Пульс/);
 assert.equal(describeClassification(row,'en').type,'Remote control ?');
 assert.match(describeClassification({...row,classification:{...row.classification,conflict:true,truncated:true}},'en').basis,/Conflicting claims; Claim limit/);
 assert.equal(describeClassification({}).type,'—');
});
test('all classification columns can be sorted without moving equal identities unpredictably',()=>{
 const rows=['watch','phone'].map((type,i)=>({address:String(i),address_type:0,sid:255,classification:{type,strength:'advertised',source:'appearance',value:i+1,capabilities:[]}}));
 for(const key of ['kind','capabilities','basis']){assert.ok(observationSortKeys.includes(key));assert.equal(sortObservations(rows,key,'ascending','en',0,()=>'',).length,2);}
 assert.equal(sortObservations(rows,'kind','ascending','en',0,()=> '')[0].classification.type,'phone');
});
