// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
// Presentation of backend claims only. No device-name/vendor heuristics here.
const kinds={unknown:['Не определён','Unknown'],phone:['Телефон','Phone'],computer:['Компьютер','Computer'],watch:['Часы','Watch'],
 remote_control:['Пульт','Remote control'],keyboard:['Клавиатура','Keyboard'],mouse:['Мышь','Mouse'],joystick:['Джойстик','Joystick'],gamepad:['Геймпад','Gamepad'],
 tag:['Метка','Tag'],heart_rate_sensor:['Пульсометр','Heart rate sensor'],thermometer:['Термометр','Thermometer'],blood_pressure_sensor:['Тонометр','Blood pressure sensor'],
 cycling_sensor:['Велодатчик','Cycling sensor'],weight_scale:['Весы','Weight scale'],environmental_sensor:['Датчик среды','Environmental sensor'],tire_pressure_sensor:['Датчик шин','Tire pressure sensor']};
const features={heart_rate:['Пульс','Heart rate'],hid:['Устройство ввода','Input device'],battery:['Батарея','Battery'],device_information:['Сведения об устройстве','Device information'],
 temperature:['Температура','Temperature'],blood_pressure:['Давление крови','Blood pressure'],cycling_speed_cadence:['Скорость/каденс','Speed/cadence'],cycling_power:['Веломощность','Cycling power'],
 running_speed_cadence:['Бег: скорость/каденс','Running speed/cadence'],weight:['Вес','Weight'],environmental_sensing:['Параметры среды','Environmental sensing'],
 immediate_alert:['Сигнализация','Immediate alert'],link_loss:['Потеря связи','Link loss'],tire_pressure:['Давление шин','Tire pressure']};
const sources={local_name:['Имя','Name'],appearance:['Appearance','Appearance'],service_uuid:['UUID','UUID'],service_data:['Service Data','Service Data'],handler:['Сигнатура протокола','Protocol signature'],solicitation:['Запрашивает','Requests']};
export function describeClassification(row,language='ru'){
 const c=row.classification,index=language==='ru'?0:1,label=(map,key)=>map[key]?.[index]||key,hex=value=>'0x'+Number(value).toString(16).toUpperCase().padStart(4,'0');
 if(!c)return {type:'—',capabilities:'—',basis:'—',tooltip:''};
 const hint=c.strength==='hint',type=label(kinds,c.type)+(hint||c.conflict?' ?':'');
 const capabilities=[...new Set((c.capabilities||[]).map(v=>label(features,v.key)))].join(', ')||'—';
 const basis=[];
 if(c.source&&c.source!=='none')basis.push(label(sources,c.source)+(c.value?' '+hex(c.value):''));
 for(const claim of c.capabilities||[]){const text=label(sources,claim.source)+(claim.value?' '+hex(claim.value):'');if(!basis.includes(text))basis.push(text);}
 for(const claim of c.requested||[])basis.push(label(sources,'solicitation')+' '+label(features,claim.key));
 if(c.conflict)basis.push(index?'Conflicting claims':'Противоречивые признаки');
 if(c.truncated)basis.push(index?'Claim limit reached':'Лимит признаков');
 const caution=index?'Advertised claims; connection and functions not verified.':'Заявлено в рекламе; соединение и работа функций не проверены.';
 return {type,capabilities,basis:basis.join('; ')||'—',tooltip:[type,capabilities,...basis,hint?(index?'Name hint only':'Предположение по имени/сигнатуре'):caution].join('\n')};
}
