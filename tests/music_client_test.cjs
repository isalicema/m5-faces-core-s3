const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync('music_bridge/client.js','utf8');
const good={source_name:'网易云',connection:'ready',available:true,stale:false,playing:true,title:'测试',artist:'歌手',favorite:null,position:2,duration:10,actions:['toggle'],artwork_id:''};
const response=(code,data)=>({status:code,ok:code>=200&&code<300,json:async()=>data,blob:async()=>({})});
const flush=()=>new Promise(r=>setImmediate(r));
function setup(fetcher){
 const elements={},buttons=[{dataset:{action:'toggle'},disabled:false}];
 const calls=[];const ctx={console,AbortSignal:{timeout:()=>undefined},crypto:{randomUUID:()=> 'unique-request'},
  fetch:async(path,opts)=>{calls.push([path,opts]);return fetcher(path,opts)},
  setTimeout:()=>1,clearTimeout:()=>{},requestAnimationFrame:()=>1,cancelAnimationFrame:()=>{},performance:{now:()=>0},Image:class {},URL:{createObjectURL:()=> 'blob:test',revokeObjectURL:()=>{}},
  document:{getElementById:id=>elements[id]??=( {textContent:'',dataset:{},style:{},replaceChildren(){}} ),querySelectorAll:()=>buttons},window:{handlers:{},addEventListener(name,fn){(this.handlers[name]??=[]).push(fn)}}};
 vm.createContext(ctx);vm.runInContext(source,ctx);return{ctx,elements,buttons,calls};
}
(async()=>{
 const repeat=setup(async (path,opts)=>response(200,path==='/api/action'?{ok:true,pending:true}:{...good,actions:['repeat_one','repeat_all']}));
 await flush();vm.runInContext("showApp('music')",repeat.ctx);
 for(const [key,expected] of [['c','repeat_one'],['C','repeat_one'],['v','repeat_all'],['V','repeat_all']]){
  const event={key,repeat:false,target:{tagName:'BODY'},preventDefault(){}};
  repeat.ctx.window.handlers.keydown.forEach(fn=>fn(event));await flush();
  const sent=repeat.calls.filter(c=>c[0]==='/api/action').at(-1);
  assert.equal(JSON.parse(sent[1].body).action,expected);
 }
 vm.runInContext("state={...state,repeat_requested:'one',stale:false};renderRepeat()",repeat.ctx);
 assert.equal(repeat.elements.repeatLabel.textContent,'已请求单曲循环');assert.equal(repeat.elements.repeatOne.dataset.requested,'true');
 vm.runInContext("state={...state,stale:true};renderRepeat()",repeat.ctx);
 assert.equal(repeat.elements.repeatLabel.textContent,'循环模式未同步');assert.equal(repeat.elements.repeatOne.dataset.requested,'false');
 vm.runInContext("state={...state,repeat_mode:'all',repeat_requested:'one',stale:false};renderRepeat()",repeat.ctx);
 assert.equal(repeat.elements.repeatLabel.textContent,'列表循环');assert.equal(repeat.elements.repeatAll.dataset.selected,'true');assert.equal(repeat.elements.repeatOne.dataset.selected,'false');
 vm.runInContext("state={...state,repeat_mode:'off'};renderRepeat()",repeat.ctx);
 assert.equal(repeat.elements.repeatOne.dataset.selected,'false');assert.equal(repeat.elements.repeatAll.dataset.selected,'false');
 vm.runInContext("state={...state,repeat_mode:'one',stale:true};renderRepeat()",repeat.ctx);
 assert.equal(repeat.elements.repeatOne.dataset.selected,'false');
 let authorized=false;
 const a=setup(async path=>{if(path==='/session'){authorized=true;return response(200,{})}return response(authorized?200:401,good)});
 await flush();assert.equal(a.elements.title.textContent,'测试');assert.equal(a.calls.filter(c=>c[0]==='/session').length,1);
 // Home alone enters companion after 180s; input/return starts a fresh window.
 vm.runInContext("showApp('home');performance.now=()=>179999;homeIdleTick()",a.ctx);
 assert.equal(a.elements.screen.dataset.app,'home');
 vm.runInContext("performance.now=()=>180000;homeIdleTick()",a.ctx);
 assert.equal(a.elements.screen.dataset.app,'companion');
 vm.runInContext("showApp('home');performance.now=()=>359999;homeIdleTick()",a.ctx);
 assert.equal(a.elements.screen.dataset.app,'home');
 vm.runInContext("homePointerHeld=true;performance.now=()=>360000;homeIdleTick()",a.ctx);
 assert.equal(a.elements.screen.dataset.app,'home');
 vm.runInContext("homePointerHeld=false;homeKeysHeld.add('ShiftLeft');homeIdleTick()",a.ctx);
 assert.equal(a.elements.screen.dataset.app,'home');
 for(const app of ['music','radio']){
  vm.runInContext(`showApp('${app}');homeIdleAt=0;performance.now=()=>999999;homeIdleTick()`,a.ctx);
  assert.equal(a.elements.screen.dataset.app,app);
 }
 vm.runInContext("performance.now=()=>0;showApp('home')",a.ctx);
 // Simulate Bridge restart invalidating the cookie; recover without page reload.
 authorized=false;await vm.runInContext('update()',a.ctx);assert.equal(a.calls.filter(c=>c[0]==='/session').length,2);assert.equal(a.buttons[0].disabled,false);
 let imageAttempts=0;
 const b=setup(async path=>{if(path.startsWith('/api/artwork')){imageAttempts++;if(imageAttempts===1)throw Error('offline');return response(200,{})}return response(200,{...good,artwork_id:'cover1'})});
 await flush();assert.equal(b.elements.status.textContent,'PLAYING');assert.equal(vm.runInContext('artId',b.ctx),'');
 await vm.runInContext('update()',b.ctx);assert.equal(imageAttempts,2);assert.equal(vm.runInContext('artId',b.ctx),'cover1');
 const c=setup(async path=>response(path==='/api/action'?401:200,path==='/api/state'?good:{}));await flush();
 await vm.runInContext('act("toggle")',c.ctx);assert.equal(c.calls.filter(x=>x[0]==='/api/action').length,1);assert.match(c.elements.notice.textContent,/重新按一次/);
 let blocked=true;const d=setup(async()=>{if(blocked)throw Error('offline');return response(200,good)});await flush();
 assert.equal(d.buttons[0].disabled,true);assert.equal(d.elements.status.textContent,'重新连接中');blocked=false;
 await vm.runInContext('update()',d.ctx);assert.equal(d.buttons[0].disabled,false);assert.equal(d.elements.status.textContent,'PLAYING');
 // Only a confirmed pause reveals the record; stale false-playing snapshots
 // must not be mistaken for a user pause. The demo sends no player command.
 assert.equal(d.elements.recordArt.dataset.paused,'false');
 vm.runInContext('state = {...state, playing:false, stale:true}; recordMotion()',d.ctx);
 assert.equal(d.elements.recordArt.dataset.paused,'false');
 vm.runInContext('state.stale=false; recordMotion()',d.ctx);
 assert.equal(d.elements.recordArt.dataset.paused,'true');
 vm.runInContext('state.playing=true; recordMotion()',d.ctx);
 assert.equal(d.elements.recordArt.dataset.paused,'false');
 const requests=d.calls.length;d.elements.motionDemo.onclick();
 assert.equal(d.elements.recordArt.dataset.paused,'false');assert.equal(d.calls.length,requests);
 // Sample the actual timeline rather than only its final class/flag.
 const e=setup(async()=>response(200,good));await flush();
 vm.runInContext('poseRecord(true);stepRecord(178.5)',e.ctx);
 assert.match(e.elements.vinyl.style.transform,/translateX\(0px\) rotate\(0deg\)/);
 const halfScale=Number(e.elements.recordArt.style.transform.match(/[\d.]+/)[0]);
 assert(halfScale<1 && halfScale>.859375);
 vm.runInContext('stepRecord(357)',e.ctx);
 assert.equal(e.elements.recordArt.style.transform,'scale(0.859375)');
 vm.runInContext('stepRecord(700)',e.ctx);
 const rolling=e.elements.vinyl.style.transform.match(/translateX\(([\d.]+)px\) rotate\(([\d.]+)deg/);
 assert(Number(rolling[1])>0 && Number(rolling[1])<27);assert(Number(rolling[2])>0);
 const beforeReverse=vm.runInContext('recordPhase',e.ctx);
 vm.runInContext('poseRecord(false)',e.ctx);
 assert.equal(vm.runInContext('recordPhase',e.ctx),beforeReverse);
 vm.runInContext('stepRecord(900)',e.ctx);
 assert(vm.runInContext('recordPhase',e.ctx)<beforeReverse);
 assert.equal(e.elements.recordArt.style.transform,'scale(0.859375)');
 vm.runInContext('stepRecord(1043)',e.ctx);
 assert(Math.abs(Number(e.elements.vinyl.style.transform.match(/translateX\(([^p]+)px/)[1]))<1e-8);
 vm.runInContext('stepRecord(1500)',e.ctx);
 assert.equal(e.elements.recordArt.style.transform,'scale(1)');
 assert.equal(vm.runInContext('recordFrame',e.ctx),null);
 let stageWanted='warm',stageFail=true,stageGets=0;
 const f=setup(async path=>{
   if(path.startsWith('/api/artwork/')){stageGets++;if(stageFail)throw Error('temporary');return response(200,{})}
   return response(200,{...good,theme:{scene_id:stageWanted,base:'#59432a'}});
 });await flush();
 assert.equal(f.elements.status.textContent,'PLAYING');assert.equal(vm.runInContext('sceneId',f.ctx),'');
 stageFail=false;await vm.runInContext('update()',f.ctx);await flush();
 assert.equal(vm.runInContext('sceneId',f.ctx),'warm');assert.equal(stageGets,2);
 assert.equal(f.elements.screen.style.backgroundColor,'#59432a');
 stageWanted='cool';await vm.runInContext('update()',f.ctx);await flush();
 assert.equal(vm.runInContext('sceneId',f.ctx),'cool');
 assert.equal(f.elements.stage.style.opacity,'1');assert.equal(f.elements.stageNext.style.opacity,'0');
 stageWanted='';await vm.runInContext('update()',f.ctx);await flush();
 assert.equal(f.elements.stage.style.backgroundImage,'none');assert.equal(f.elements.screen.style.backgroundColor,'#202a35');
 // A stalled old background must neither block metadata nor overwrite a new song.
 let releaseOld, delayedScene='slow';
 const slow=setup(async path=>{
   if(path==='/api/artwork/slow')return await new Promise(resolve=>{releaseOld=()=>resolve(response(200,{}))});
   if(path.startsWith('/api/artwork/'))return response(200,{});
   return response(200,{...good,title:delayedScene,theme:{scene_id:delayedScene,base:'#334455'}});
 });await flush();
 assert.equal(slow.elements.title.textContent,'slow');assert.equal(vm.runInContext('updating',slow.ctx),false);
 delayedScene='new';await vm.runInContext('update()',slow.ctx);await flush();
 assert.equal(slow.elements.title.textContent,'new');assert.equal(vm.runInContext('sceneId',slow.ctx),'new');
 releaseOld();await flush();assert.equal(vm.runInContext('sceneId',slow.ctx),'new');
 // Resetting to no cover also invalidates an outstanding backdrop.
 delayedScene='slow';await vm.runInContext('update()',slow.ctx);await flush();
 delayedScene='';await vm.runInContext('update()',slow.ctx);await flush();releaseOld();await flush();
 assert.equal(vm.runInContext('sceneId',slow.ctx),'');
 const g=setup(async()=>response(200,good));await flush();
 vm.runInContext("state={...state,position:8,lyric:{status:'synced',lines:[[0,'first'],[10,'second']]}};lyricStateAt=0;performance.now=()=>5000;renderLyric()",g.ctx);
 assert.equal(g.elements.lyricText.textContent,'second');
 vm.runInContext('state.playing=false;renderLyric()',g.ctx);
 assert.equal(g.elements.lyricText.textContent,'first');
 vm.runInContext('state.playing=true;state.stale=true;renderLyric()',g.ctx);
 assert.equal(g.elements.lyricText.textContent,'first');
 vm.runInContext('state.position=1;renderLyric()',g.ctx);
 assert.equal(g.elements.lyricText.textContent,'first');
 const h=setup(async()=>response(200,good));await flush();
 assert.equal(h.elements.screen.dataset.app,'home');const menuRequests=h.calls.length;
 h.elements.openMusic.onclick();assert.equal(h.elements.screen.dataset.app,'music');
 h.elements.homeButton.onclick();assert.equal(h.elements.screen.dataset.app,'home');
 h.elements.openRadio.onclick();assert.equal(h.elements.screen.dataset.app,'radio');
 h.elements.radioHome.onclick();assert.equal(h.elements.screen.dataset.app,'home');
 assert.equal(h.calls.length,menuRequests);
 let batteryReport={received_at:Date.now()/1000,power:{valid:true,battery_present:true,usb_present:true,charging:true,percent:72}};
 const powerView=setup(async path=>response(200,path==='/api/ota/status'?{device:batteryReport}:good));await flush();
 assert.equal(powerView.elements.homePowerPercent.textContent,'72%');assert.equal(powerView.elements.homePower.title,'充电中');
 assert.equal(powerView.elements.homePower.dataset.state,'charging');assert.equal(powerView.elements.homePowerFill.style.width,'20px');
 batteryReport={...batteryReport,received_at:Date.now()/1000-180};
 await vm.runInContext('powerPollAt=0;updatePower()',powerView.ctx);
 assert.equal(powerView.elements.homePowerPercent.textContent,'--');assert.equal(powerView.elements.homePower.title,'电量待同步');
 batteryReport={received_at:Date.now()/1000,power:{valid:true,battery_present:false,usb_present:true,charging:false,percent:null}};
 await vm.runInContext('powerPollAt=0;updatePower()',powerView.ctx);
 assert.equal(powerView.elements.homePowerPercent.textContent,'--');assert.equal(powerView.elements.homePower.title,'未检测到电池');
 for(const [level,state] of [[31,'normal'],[30,'low'],[16,'low'],[15,'critical'],[0,'critical']]){
   batteryReport={received_at:Date.now()/1000,power:{valid:true,battery_present:true,usb_present:false,charging:false,percent:level}};
   await vm.runInContext('powerPollAt=0;updatePower()',powerView.ctx);assert.equal(powerView.elements.homePower.dataset.state,state);
 }
 console.log('PASS: timed lyrics/pause/stale/seek; stage retry/crossfade/reset; ordered shrink/roll, rotation, mid-motion reversal, idle stop; confirmed pause, stale-state hold, visual-only demo; session restart, artwork retry, no action replay, reconnect recovery');
})().catch(e=>{console.error(e);process.exitCode=1});
