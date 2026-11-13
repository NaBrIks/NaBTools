// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
export function scanModeControl(element,command,t){
 element.onchange=()=>{
  if(!element.disabled)command({scan_mode:element.value},undefined,t('scan_mode_accepted'));
 };
 return (runtime,fixture)=>{
  if(document.activeElement!==element)element.value=runtime.requested_scan_mode||runtime.scan_mode||'passive';
  element.disabled=fixture||!runtime.scan_mode_supported||runtime.scan_mode_pending;
  element.dataset.tooltip=t('scan_mode_help');
 };
}
