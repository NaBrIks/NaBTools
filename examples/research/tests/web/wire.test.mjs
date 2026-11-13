// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {decode as independent,encode as independentEncode} from 'cborg';
import {decode,encode} from '../../web/cbor.js';
import {fields} from '../../web/cbor-values.js';
test('native research snapshots decode independently and in browser codec',async()=>{
 const bytes=await readFile(new URL('../../../../local/native-build/research-0.cbor',import.meta.url));
 const a=independent(bytes),b=decode(bytes);
 assert.equal(a.runtime.schema,'nabtools.research.v1');
 assert.equal(b.runtime.observations.length,a.runtime.observations.length);
 assert.equal(b.runtime.observations[0].manufacturer_id,117);
 assert.equal(b.runtime.observations[1].manufacturer_id,135);
 const radio=b.runtime.observations[0].radio;
 assert.equal(radio.primary_phy.name,'LE 1M');assert.equal(radio.primary_phy.code,1);
 assert.equal(radio.tx_power_dbm,-12);assert.equal(radio.periodic_interval_us,100000);
 assert.equal(radio.connectable,true);assert.equal(radio.data_status.name,'Complete');
 assert.equal(b.runtime.observations[1].radio,null);
 const remote=b.runtime.observations[5].classification,watch=b.runtime.observations[6].classification;
 assert.equal(remote.type,'remote_control');assert.equal(remote.strength,'hint');
 assert.equal(remote.capabilities[0].key,'hid');assert.equal(remote.capabilities[0].value,0x1812);
 assert.equal(watch.type,'watch');assert.equal(watch.source,'appearance');
 assert.deepEqual(watch.capabilities.map(c=>c.key),['heart_rate','battery']);
});
test('native TPMS CBOR has correct units and signed temperature',async()=>{
 for(const mode of ['cbor','both']){
  const bytes=await readFile(new URL(`../../../../local/native-build/${mode}.cbor`,import.meta.url));
  const a=independent(bytes);assert.equal(a.schema,'nabtools.tpms.v1');
  assert.equal(a.pressure_pa,250000);assert.equal(a.temperature_centi_c,-1234);
  assert.equal(decode(bytes).temperature_centi_c,-1234);
 }
});
test('browser commands interoperate with independent CBOR implementation',()=>{
 for(const value of [{scan:true},{scan:false},{scan_mode:'active'},{scan_mode:'passive'},{remove_slot:31,generation:4294967295}]){
  assert.deepEqual(independent(encode(value)),value);
  assert.deepEqual({...decode(independentEncode(value))},value);
 }
});
test('SmartTag CBOR parses real packed fields and never invents a button',async()=>{
 for(const mode of ['cbor','both']){
  const b=await readFile(new URL(`../../../../local/native-build/smarttag-${mode}.cbor`,import.meta.url));
  const p=independent(b);assert.equal(p.version,1);assert.equal(p.tag_state,5);
  assert.equal(p.aging_counter,0x03601f);assert.equal(p.battery_level,2);assert.equal(p.region_id,11);
  assert.equal(p.privacy_id.length,8);assert.equal(p.signature.length,4);assert.equal(p.type,'advertisement');
  assert.equal(p.button,undefined);assert.equal(decode(b).raw.length,20);
 }
});
test('reject truncation, duplicate keys, trailing and unsupported values',()=>{
 const good=encode({scan:true});for(let n=0;n<good.length;n++)assert.throws(()=>decode(good.slice(0,n)));
 for(const bytes of [[0xa2,0x61,0x61,0,0x61,0x61,1],[0xf5,0],[0x9f,0xff],[0x61,0xff]])assert.throws(()=>decode(Uint8Array.from(bytes)));
 assert.throws(()=>encode(1.5));
});

test('Heart Rate direct CBOR preserves units, optional contact and RR order',async()=>{
 for(const mode of ['cbor','both']){
  const bytes=await readFile(new URL(`../../../../local/native-build/hrm-${mode}.cbor`,import.meta.url));
  const value=independent(bytes);assert.equal(value.type,'measurement');assert.equal(value.heart_rate_bpm,300);
  assert.equal(value.energy_kj,4660);assert.equal(value.contact_supported,true);assert.equal(value.contact_detected,true);
  assert.deepEqual(value.rr_1_1024_s,[1024,512]);assert.equal(value.standard_layout,true);
  assert.equal(decode(bytes).heart_rate_bpm,300);
 }
});
test('generic CBOR view includes arbitrary nested fields without device branches',()=>{
 const value={pressure_pa:250000,custom:{'a/b':[-1234,true,null]},signature:new Uint8Array([0xab,0])};
 const rows=fields(value);assert.deepEqual(rows.map(r=>r.path),['/pressure_pa','/custom/a~1b/0','/custom/a~1b/1','/custom/a~1b/2','/signature']);
 assert.equal(rows[1].value,'-1234');assert.equal(rows[4].value,'ab 00');
});
test('application journal owns borrowed bytes and retains a bounded ordered history',async()=>{
 const load=async suffix=>independent(await readFile(new URL('../../../../local/native-build/smarttag-cbor.cbor.'+suffix,import.meta.url)));
 const copied=await load('journal');assert.equal(copied.length,1);assert.deepEqual([...copied[0].payload],[0x18,0x42]);
 const wrapped=await load('wrapped');assert.equal(wrapped.length,16);assert.equal(wrapped[0].ordinal,6);assert.equal(wrapped.at(-1).ordinal,21);
 assert.equal(wrapped[0].generation,1);assert.equal(wrapped[0].schema,'test.v1');
});
