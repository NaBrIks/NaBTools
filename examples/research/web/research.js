// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
import {ResearchSocket} from './live-socket.js';
import './ui-tooltip.js';
import {strings} from './i18n.js';
import {devicePage} from './devices.js';
import {observationSortKeys} from './observation-sort.js';
import {observationTable} from './observations.js';
import {setText} from './dom.js';
import {scanModeControl} from './scan-mode.js';
const $=id=>document.getElementById(id),shell=$('shell');
const themes=[['light','Светлая','Light','sun'],['cream','Кремовая','Cream','ice-cream'],['emerald','Изумрудная','Emerald','trees'],['dark','Тёмная','Dark','moon']];
const densities=[['nano','Ультракомпактная','Ultra compact','baseline-density-small'],['mini','Компактная','Compact','baseline-density-medium'],['norm','Обычная','Normal','baseline-density-large']];
const read=(k,fallback)=>{try{return localStorage.getItem('nabtools.'+k)||fallback}catch{return fallback}};
const save=(k,v)=>{try{localStorage.setItem('nabtools.'+k,v)}catch{}};
let sortKey=read('observation-sort','company'),sortDirection=read('observation-direction','ascending');
if(!observationSortKeys.includes(sortKey))sortKey='company';
if(!['ascending','descending'].includes(sortDirection))sortDirection='ascending';
let language=read('language','ru'),snapshot=null,lastSuccess=0,receivedAt=0;
let requestSerial=Date.now();const pendingControls=new Map();
if(!strings[language])language='ru';
const t=key=>strings[language][key]||key;
shell.dataset.theme=themes.some(x=>x[0]===read('theme','cream'))?read('theme','cream'):'cream';
shell.dataset.density=densities.some(x=>x[0]===read('density','nano'))?read('density','nano'):'nano';
function node(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n}
function appearance(){
 const menu=$('appearance-menu');menu.replaceChildren();
 for(const [kind,list,caption] of [['theme',themes,'themes'],['density',densities,'density']]){
  menu.append(node('strong',t(caption)));
  for(const [id,ru,en,icon] of list){const selected=shell.dataset[kind]===id,b=node('button',undefined,selected?'selected':'');
   const i=node('i',undefined,'ti ti-'+icon);i.setAttribute('aria-hidden','true');b.append(i,document.createTextNode(' '+(language==='ru'?ru:en)+(selected?' ✓':'')));
   b.setAttribute('aria-pressed',String(selected));b.onclick=()=>{shell.dataset[kind]=id;save(kind,id);appearance()};menu.append(b);
  }
 }
 const current=themes.find(x=>x[0]===shell.dataset.theme),label=$('appearance-label');label.replaceChildren(node('i',undefined,'ti ti-palette'),document.createTextNode(' '+current[language==='ru'?1:2]+' · '+shell.dataset.density.toUpperCase()));
}
function localize(){document.documentElement.lang=language;$('language').textContent=language.toUpperCase();
 document.querySelectorAll('[data-t]').forEach(n=>n.textContent=t(n.dataset.t));document.querySelectorAll('.shell-sidebar a').forEach(n=>n.dataset.tooltip=n.textContent);$('filter').placeholder=t('filter');appearance();sortHeaders();render();}
let feedbackUntil=0;
function message(text,error=false,hold=false){setText($('message'),text);$('message').className=error?'error':'';if(hold)feedbackUntil=Date.now()+5000;}
const hex=bytes=>Array.from(bytes||[],n=>n.toString(16).padStart(2,'0')).join(' ');
function details(title,data){$('details-title').textContent=title;$('details-content').textContent=typeof data==='string'?data:JSON.stringify(data,(_k,v)=>v instanceof Uint8Array?hex(v):v,2);$('details').showModal();}
const renderDevices=devicePage({$,node,t,details,command});
const renderObservations=observationTable({$,node,t,details,command,onAdded:result=>{renderDevices.select(result);location.hash='#devices'}});
const renderScanMode=scanModeControl($('scan-mode'),command,t);
function render(){
 if(!snapshot)return;
 const r=snapshot.runtime,fixture=r.source==='native fixture',now=r.now_ms+(fixture?0:Date.now()-receivedAt);
 setText($('connection'),(fixture?t('fixture'):t('live'))+' · '+r.source+' · '+(client.status==='live'?'WS / CBOR':client.status));$('connection').className='shell-stats'+(fixture?' fixture':'');
 setText($('scan-state'),r.scan+' · '+(r.scan_mode||'passive')+(r.scan_mode_pending?' → '+r.requested_scan_mode:''));$('scan-state').className=r.scan==='fault'?'error':'';
 renderScanMode(r,fixture);
 setText($('totals'),`${r.observed} packets · ${r.dropped} dropped · ${r.malformed} malformed · ${r.fragments} fragments · SIG ${r.sig}`);
 $('start').disabled=fixture||r.scan==='scanning'||r.scan==='starting';$('pause').disabled=fixture||r.scan!=='scanning';
 if(!$('overview').hidden)renderObservations(r,{sortKey,sortDirection,language,now});
 if(!$('devices-page').hidden)renderDevices(snapshot);
 if(!$('settings').hidden){const settings=$('settings-values');const pairs=[['source',r.source],['sig',r.sig],['transport','WebSocket / CBOR'],['scope',t('scope_value')]];
 if(!settings.children.length)for(const _ of pairs)settings.append(node('dt'),node('dd'));
 pairs.forEach(([k,v],i)=>{setText(settings.children[i*2],t(k));setText(settings.children[i*2+1],v)})}

}
function processControls(next){
 for(const result of next.controls||[]){const pending=pendingControls.get(result.request_id);if(!pending)continue;
  pendingControls.delete(result.request_id);message(result.result==='ok'?(pending.acceptedMessage||t('applied')):result.result,result.result!=='ok',true);
  if(result.result==='ok')pending.success?.(result);
 }
}
let renderScheduled=false;
function scheduleRender(){if(!renderScheduled){renderScheduled=true;requestAnimationFrame(()=>{renderScheduled=false;render()})}}
document.addEventListener('selectionchange',scheduleRender);
const client=new ResearchSocket({url:new URL('/ws',location.href).href.replace(/^http/,'ws'),
 onState(next,clockChanged){
  if(snapshot&&snapshot.boot_id!==next.boot_id)pendingControls.clear();
  snapshot=next;lastSuccess=Date.now();if(clockChanged)receivedAt=lastSuccess;
  scheduleRender();processControls(next);if(Date.now()>feedbackUntil)message(new Date(lastSuccess).toLocaleTimeString(language==='ru'?'ru-RU':'en-GB'));
 },
 onAck(ack){if(ack.status==='queued'){message(t('queued'),false,true);return}
  pendingControls.delete(ack.request_id);message(ack.status,true,true);
 },
 onStatus(status,error){if(error)message(error,true);else if(status!=='live')message(status,status==='offline');scheduleRender()}
});
function refresh(){client.recover()}
function command(value,success,acceptedMessage){const request_id=++requestSerial;try{
 pendingControls.set(request_id,{success,acceptedMessage,started:Date.now()});
 client.send({...value,request_id});
 }catch(error){pendingControls.delete(request_id);message(String(error),true,true)}}
setInterval(()=>{for(const [id,pending]of pendingControls)if(Date.now()-pending.started>10000){
 pendingControls.delete(id);message(language==='ru'?'Результат команды не получен; проверьте состояние устройства.':'Command result unavailable; check device state.',true,true);
}},1000);
$('language').onclick=()=>{language=language==='ru'?'en':'ru';save('language',language);localize()};
$('menu').onclick=()=>{$('menu').setAttribute('aria-expanded',String(!shell.classList.toggle('menu-collapsed')))};
$('refresh').onclick=refresh;$('filter').oninput=render;$('start').onclick=()=>command({scan:true});$('pause').onclick=()=>command({scan:false});
function sortHeaders(){for(const button of document.querySelectorAll('#observations [data-sort]')){
 button.setAttribute('aria-label',button.textContent);
 button.closest('th').setAttribute('aria-sort',button.dataset.sort===sortKey?sortDirection:'none');
}}
for(const button of document.querySelectorAll('#observations [data-sort]'))button.onclick=()=>{
 sortDirection=button.dataset.sort===sortKey&&sortDirection==='ascending'?'descending':'ascending';sortKey=button.dataset.sort;
 save('observation-sort',sortKey);save('observation-direction',sortDirection);sortHeaders();render();
};sortHeaders();
$('close').onclick=()=>$('details').close();$('help').onclick=()=>details('NaBTools research',t('scope_value')+'\n\n'+(language==='ru'?'Просмотр эфира не подключается к приборам. Соединение запускается явной командой выбранного экземпляра. COMMON — курируемые имена, а не рейтинг популярности. Показания native fixture синтетические.':'Advertising view does not connect to devices. Use an explicit command on the selected instance to connect. COMMON is curated coverage, not a popularity ranking. Native fixture data is synthetic.'));
function navigate(){const page=location.hash==='#settings'?'settings':['#devices','#tpms'].includes(location.hash)?'devices':'overview';
 for(const [name,id] of [['overview','overview'],['devices','devices-page'],['settings','settings']]){
  $(id).hidden=page!==name;$(name+'-link').classList.toggle('active',page===name);
 }render();}
window.addEventListener('hashchange',navigate);localize();navigate();client.start();
