// GETs may recover a lost browser session. Actions are never retried automatically.
let sessionFlight = null;
async function reconnect() {
    if (!sessionFlight) sessionFlight = fetch('/session', {
        method: 'POST', headers: {'X-Faces-Local': '1'}, signal: AbortSignal.timeout(4000)
    }).then(r => { if (!r.ok) throw Error('session unavailable'); })
      .finally(() => { sessionFlight = null; });
    return sessionFlight;
}
async function read(path) {
    let r = await fetch(path, {signal: AbortSignal.timeout(5000)});
    if (r.status === 401) {
        await reconnect();
        r = await fetch(path, {signal: AbortSignal.timeout(5000)});
    }
    if (!r.ok) throw Error('bridge unavailable');
    return r;
}
let state = {}, artId = '', artUrl = '', pending = false, noticeUntil = 0, updating = false;
const $ = id => document.getElementById(id);
const fmt = x => Math.floor((x || 0)/60)+':'+String(Math.floor((x || 0)%60)).padStart(2,'0');
function notice(text) { $('notice').textContent=text; noticeUntil=Date.now()+4000; }

// Preview-only presentation. Source data and device rendering are unchanged.
const paperIcon=name=>'<svg class="paper-icon" viewBox="0 0 256 256" aria-hidden="true"><use href="#paper-'+name+'"/></svg>';
function paperAccent(theme) {
    const color=/^#[0-9a-f]{6}$/i.test(theme?.dominant)?theme.dominant:'#d7c9b2';
    const linear=color.slice(1).match(/../g).map(x=>parseInt(x,16)/255).map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4);
    const l=linear[0]*.2126+linear[1]*.7152+linear[2]*.0722;
    $('screen').style.setProperty('--music-accent',color);
    $('screen').style.setProperty('--music-accent-ink',l>.179?'#000000':'#ffffff');
}

function renderRepeat() {
    const confirmed=!state.stale && ['off','one','all'].includes(state.repeat_mode);
    const mode=confirmed?state.repeat_mode:!state.stale && ['one','all'].includes(state.repeat_requested)?state.repeat_requested:'';
    $('repeatOne').dataset.selected=String(confirmed && mode==='one');
    $('repeatAll').dataset.selected=String(confirmed && mode==='all');
    $('repeatLabel').textContent=confirmed?(mode==='off'?'循环关闭':(mode==='one'?'单曲':'列表')+'循环'):mode?'已请求'+(mode==='one'?'单曲':'列表')+'循环':'循环模式未同步';
    $('repeatOne').dataset.requested=String(mode==='one');
    $('repeatAll').dataset.requested=String(mode==='all');
    $('repeatLabel').hidden=!confirmed;
    for(const [id,value,label,key] of [['repeatOne','one','单曲循环','C'],['repeatAll','all','列表循环','V']]) {
        const button=$(id),requested=!confirmed&&mode===value;
        button.dataset.unconfirmed=String(requested);
        button.ariaPressed=confirmed?String(mode===value):requested?'mixed':'false';
        button.title=label+' · '+key+(requested?'（设置指令已发送，播放器未提供实时模式回读）':confirmed&&mode===value?'（已确认）':'');
    }

}
function controls() {
    document.querySelectorAll('[data-action]').forEach(b => {
        b.disabled = pending || state.stale || !(state.actions || []).includes(b.dataset.action);
    });
}
// A reversible timeline: sleeve shrinks first, then the record rolls out.
// Walking the same timeline backwards rolls it in before enlarging the sleeve.
let currentApp='home',homeIdleAt=performance.now(),homePointerHeld=false;
const homeKeysHeld=new Set(),HOME_IDLE_MS=180000;
function homeIdleTick(){
    if(currentApp==='home'&&!homePointerHeld&&!homeKeysHeld.size&&performance.now()-homeIdleAt>=HOME_IDLE_MS)showApp('companion');
}
function pollHomeIdle(){homeIdleTick();setTimeout(pollHomeIdle,500);}
setTimeout(pollHomeIdle,500);
window.addEventListener('pointerdown',e=>{if(currentApp==='home'&&e.target.closest?.('#screen')){homePointerHeld=true;homeIdleAt=performance.now();}});
window.addEventListener('pointerup',()=>{if(homePointerHeld)homeIdleAt=performance.now();homePointerHeld=false;});
window.addEventListener('pointercancel',()=>{if(homePointerHeld)homeIdleAt=performance.now();homePointerHeld=false;});
window.addEventListener('keydown',e=>{if(currentApp==='home'){homeKeysHeld.add(e.code||e.key);homeIdleAt=performance.now();}});
window.addEventListener('keyup',e=>{if(homeKeysHeld.delete(e.code||e.key))homeIdleAt=performance.now();});
window.addEventListener('blur',()=>{homeKeysHeld.clear();homePointerHeld=false;homeIdleAt=performance.now();});
let homeLaunchTimer=0,homeLaunchTarget='';
const homeCards={music:'openMusic',shelf:'openShelfHome',radio:'openRadio',companion:'openCompanion'};
function pressHomeCard(app){
    if(currentApp!=='home'||homeLaunchTarget)return;
    homeLaunchTarget=app;
    $(homeCards[app]).dataset.pressed='true';
    // Let a quick tap paint the full press before navigating. No player command.
    homeLaunchTimer=setTimeout(()=>app==='shelf'?openShelf():showApp(app),160);
}
function showApp(app) {
    clearTimeout(homeLaunchTimer);homeLaunchTimer=0;homeLaunchTarget='';
    [...new Set(Object.values(homeCards))].forEach(id=>{$(id).dataset.pressed='false'});
    if(currentApp==='shelf' && app!=='shelf')stopShelf();
    currentApp=['music','shelf','radio','companion','connection'].includes(app)?app:'home';
    homeIdleAt=performance.now();homeKeysHeld.clear();homePointerHeld=false;
    $('screen').dataset.app=currentApp;
    $('companionNearDemo').hidden=currentApp!=='companion';
    $('radioHint').hidden=currentApp!=='radio';
    ['musicKeys','musicHint','notice','motionDemo'].forEach(id=>{$(id).hidden=currentApp!=='music'});
    window.history?.replaceState(null,'',currentApp==='home'?'#home':'#'+currentApp);
}
function networkPreview(mode){
    $('connectionRows').hidden=mode==='edit';$('connectionPortal').hidden=mode!=='edit';
    $('connectionStatus').textContent=mode==='edit'?'手机配网流程预览':mode==='work'?'公司网络未配置':mode==='home'?'预览：家庭网络已连接':'预览：自动选择可用网络';
    $('networkAuto').dataset.active=String(mode==='auto');$('networkHome').dataset.active=String(mode==='home');
}
$('homeConnection').onclick=$('radioConnection').onclick=()=>showApp('connection');
$('connectionHome').onclick=()=>showApp('home');
$('networkAuto').onclick=()=>networkPreview('auto');$('networkHome').onclick=()=>networkPreview('home');
$('networkWork').onclick=()=>networkPreview('work');$('networkEdit').onclick=()=>networkPreview('edit');
$('openCompanion').onclick=()=>pressHomeCard('companion');
$('companionHome').onclick=()=>showApp('home');
$('openMusic').onclick=()=>pressHomeCard('music');$('openShelfHome').onclick=()=>pressHomeCard('shelf');
$('openRadio').onclick=()=>pressHomeCard('radio');
$('homeButton').onclick=$('radioHome').onclick=()=>showApp('home');
// Radio interactions are local demonstrations only: never dispatch Mac actions.
const radioStationsPreview=['Groove Salad','Drone Zone','Secret Agent'];
let radioIndex=0,radioPlaying=false,radioStarted=false,radioVolume=48;
function renderRadio(){
    $('radioView').dataset.station=String(radioIndex);
    $('radioStatus').textContent=radioPlaying?'LIVE':radioStarted?'PAUSED':'READY';
    $('radioTrack').textContent=radioPlaying?'SomaFM · '+radioStationsPreview[radioIndex]: radioStarted?'Paused · SPACE to listen':'Choose a station to listen';
    $('radioNeedle').style.left=(253.5+radioIndex*20)+'px';
    $('radioPlay').innerHTML=paperIcon(radioPlaying?'pause':'play');
    $('radioPlay').ariaLabel=(radioPlaying?'暂停':'播放')+'设备扬声器，空格';
    $('radioVolume').textContent='VOL '+Math.floor(radioVolume*100/160)+'%';
    for(let i=0;i<3;i++)$('radioStation'+i).ariaPressed=String(i===radioIndex);
}
function selectRadio(i){radioIndex=(i+3)%3;radioStarted=true;radioPlaying=true;renderRadio();}
function toggleRadio(){radioStarted=true;radioPlaying=!radioPlaying;renderRadio();}
function volumeRadio(delta){radioVolume=Math.max(0,Math.min(160,radioVolume+delta));renderRadio();}
for(let i=0;i<3;i++)$('radioStation'+i).onclick=()=>selectRadio(i);
$('radioPlay').onclick=toggleRadio;
$('radioVolumeDown').onclick=()=>volumeRadio(-8);$('radioVolumeUp').onclick=()=>volumeRadio(8);
renderRadio();
let powerPollAt=0,powerPending=false;
async function updatePower(){
    if(!['home','music','radio','shelf'].includes(currentApp)||powerPending||Date.now()-powerPollAt<10000)return;
    powerPollAt=Date.now();powerPending=true;
    try{
        const {device}=await (await read('/api/power')).json();
        const p=device?.power,age=Date.now()-(device?.received_at||0)*1000;
        const valid=p?.valid&&age>=0&&age<120000;
        const percent=valid&&p.battery_present&&Number.isInteger(p.percent)?p.percent:null;
        const charging=valid&&p.battery_present&&p.charging;
        for(const prefix of ['homePower','musicPower','radioPower','shelfPower']){
        $(prefix+'Percent').textContent=percent===null?'--':percent+'%';
        $(prefix+'Fill').style.width=(charging?20:percent===null?0:20*Math.max(0,Math.min(100,percent))/100)+'px';
        const detail=!valid?'电量待同步':!p.battery_present?'未检测到电池':charging?'充电中':p.usb_present?'USB 供电':'电池供电';
        $(prefix).title=detail;$(prefix).ariaLabel=$(prefix+'Percent').textContent+' · '+detail;
        $(prefix).dataset.state=charging?'charging':percent!==null&&percent<=15?'critical':percent!==null&&percent<=30?'low':'normal';
        }
    }catch(error){for(const prefix of ['homePower','musicPower','radioPower','shelfPower']){$(prefix+'Percent').textContent='--';$(prefix+'Fill').style.width='0px';$(prefix).title='电量待同步';$(prefix).ariaLabel='电量待同步';$(prefix).dataset.state='normal';}}
    finally{powerPending=false;}
}
showApp(window.location?.hash?.slice(1));
let recordPaused = false, demoTimer = null, demoTimers = [];
let recordPhase = 0, recordGoal = 0, recordFrame = null, recordTime = 0;
const ease = t => { t=Math.max(0,Math.min(1,t)); return t*t*(3-2*t); };
function paintRecord() {
    const shrink=ease(recordPhase/.34), roll=ease((recordPhase-.34)/.66);
    $('recordArt').style.transform = `scale(${1-shrink*.140625})`;
    // Rotation follows travel / radius, keeping the roll tied to displacement.
    $('vinyl').style.transform = `translateX(${27*roll}px) rotate(${27/58*180/Math.PI*roll}deg)`;
}
function stepRecord(now) {
    const distance=Math.max(0,now-recordTime)/1050; recordTime=now;
    recordPhase += Math.sign(recordGoal-recordPhase)*Math.min(distance,Math.abs(recordGoal-recordPhase));
    paintRecord();
    recordFrame = recordPhase===recordGoal ? null : requestAnimationFrame(stepRecord);
}
function poseRecord(paused) {
    $('recordArt').dataset.paused=String(paused); recordGoal=paused?1:0;
    if(window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) {
        if(recordFrame!==null)cancelAnimationFrame(recordFrame);
        recordFrame=null;recordPhase=recordGoal;paintRecord();return;
    }
    if(recordFrame===null && recordPhase!==recordGoal) {
        recordTime=performance.now();recordFrame=requestAnimationFrame(stepRecord);
    }
}
function recordMotion() {
    if (state.available && !state.stale && state.connection === 'ready') recordPaused = !state.playing;
    if (!demoTimer) poseRecord(recordPaused);
}
$('motionDemo').onclick = () => {
    showApp('music');
    demoTimers.forEach(clearTimeout); demoTimer=true;
    poseRecord(false);
    $('motionDemo').textContent = '动效演示 · 缩小 → 滚出 → 滚回 → 放大';
    demoTimers=[setTimeout(()=>poseRecord(true),1150),setTimeout(()=>poseRecord(false),2850),
        setTimeout(()=>{demoTimer=null;recordMotion();$('motionDemo').textContent='试听不停 · 预览黑胶动效';},4000)];
};
let sceneId='', sceneFront=0, sceneUrls=['',''], sceneWanted='', sceneEpoch=0;
const scenePending=new Set();
async function updateStage(theme) {
    const wanted=theme?.scene_id || '';
    if(wanted!==sceneWanted){sceneWanted=wanted;sceneEpoch++;}
    const epoch=sceneEpoch;
    if(wanted===sceneId || scenePending.has(epoch))return;
    const layers=[$('stage'),$('stageNext')];
    if(!wanted) {
        sceneUrls.forEach(url=>{if(url)URL.revokeObjectURL(url)});sceneUrls=['',''];sceneId='';
        layers.forEach(layer=>{layer.style.backgroundImage='none';layer.style.opacity='0'});
        $('screen').style.backgroundColor='#202a35';return;
    }
    scenePending.add(epoch);
    try {
        const blob=await (await read('/api/artwork/'+wanted)).blob();
        if(epoch!==sceneEpoch)return;
        const back=1-sceneFront;
        if(sceneUrls[back])URL.revokeObjectURL(sceneUrls[back]);
        sceneUrls[back]=URL.createObjectURL(blob);
        layers[back].style.backgroundImage=`url("${sceneUrls[back]}")`;
        layers[back].style.opacity='1';layers[sceneFront].style.opacity='0';
        $('screen').style.backgroundColor=/^#[0-9a-f]{6}$/i.test(theme?.base)?theme.base:'#202a35';
        sceneFront=back;sceneId=wanted;
    } catch(error) { /* Keep the last backdrop; retry without disabling controls. */ }
    finally {scenePending.delete(epoch);}
}
let lyricStateAt=performance.now(), lastLyricKey='';
function renderLyric() {
    const info=state.lyric || {status:'disabled',lines:[]};
    const position=(state.position||0)+(state.playing&&!state.stale?(performance.now()-lyricStateAt)/1000:0);
    const cues=info.lines || [];
    let cue=null;for(const item of cues){if(item[0]<=position)cue=item;else break;}
    const labels={loading:'正在找歌词…',retrying:'歌词暂不可用',unavailable:'暂无同步歌词',instrumental:'纯音乐',disabled:'歌词待接入'};
    const text=info.status==='synced'?(cue?.[1] || '♪'):(labels[info.status] || '等待歌词');
    const key=state.track_id+':'+(cue?.[0]??'')+':'+text;
    const el=$('lyricText');
    if(key!==lastLyricKey){
        lastLyricKey=key;el.textContent=text;
        if(!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches)
            el.animate?.([{opacity:0,transform:'translateY(5px)'},{opacity:1,transform:'translateY(0)'}],{duration:220});
    }
    const overflow=Math.max(0,(el.scrollWidth||0)-($('lyric').clientWidth||0));
    const travel=cue?Math.max(0,position-cue[0]-1.2)*20:0;
    el.style.transform=`translateX(${-Math.min(overflow,travel)}px)`;
}
(function lyricTick(){renderLyric();setTimeout(lyricTick,200)})();
async function update() {
    if (updating) return;
    updating = true;
    try {
        state = await (await read('/api/state')).json();
        lyricStateAt=performance.now();renderLyric();
        recordMotion();
        void updateStage(state.theme);paperAccent(state.theme);
        $('source').textContent = /网易云/.test(state.source_name||'')?'网易云音乐':state.source_name || '音乐遥控器';
        $('status').textContent = state.connection === 'metadata_missing' ? '等待曲目信息' :
            state.stale ? '状态待更新' : !state.available ? '等待播放器' : state.playing ? 'PLAYING' : 'PAUSED';
        $('title').textContent = state.title;
        $('artist').textContent = state.artist;
        $('heart').dataset.liked = String(state.favorite === true);
        $('heart').innerHTML = paperIcon(state.favorite===true?'heart':'heartOutline')+'<span>喜欢</span>';
        $('favoriteControl').dataset.liked=String(state.favorite===true);
        $('favoriteControl').innerHTML=paperIcon(state.favorite===true?'heart':'heartOutline')+'<span>H</span>';
        $('play').innerHTML = paperIcon(state.playing?'pause':'play')+'<span>SPC</span>';
        $('play').ariaLabel=state.playing?'暂停，快捷键 空格':'播放，快捷键 空格';
        $('status').title=$('status').textContent;
        $('source').title=$('source').textContent;
        $('title').title=state.title||'';
        $('elapsed').textContent=fmt(state.position); $('duration').textContent=fmt(state.duration);
        $('progress').style.width=(state.duration?Math.min(100,state.position/state.duration*100):0)+'%';
        controls();renderRepeat();
        if(Date.now()>noticeUntil) $('notice').textContent=state.error || (!state.available?'等待网易云或 Apple Music 提供播放信息。':'');
        if (state.artwork_id !== artId) {
            // Commit the ID only after a successful fetch, so transient artwork
            // failures are retried on the next poll rather than stuck forever.
            if (!state.artwork_id) {
                if(artUrl) URL.revokeObjectURL(artUrl);
                artUrl=''; artId=''; $('art').textContent='♪';
            } else {
                try {
                    const requestedId=state.artwork_id;
                    const blob=await (await read('/api/artwork/'+requestedId)).blob();
                    const nextUrl=URL.createObjectURL(blob), img=new Image();
                    img.alt='当前专辑封面'; img.src=nextUrl;
                    if(artUrl)URL.revokeObjectURL(artUrl);
                    artUrl=nextUrl; artId=requestedId; $('art').replaceChildren(img);
                } catch (error) {
                    // A cover download error does not mean the Bridge is offline.
                    if(artUrl)URL.revokeObjectURL(artUrl);
                    artUrl=''; artId=''; $('art').textContent='♪';
                }
            }
        }
    } catch (error) {
        state.actions=[];state.stale=true; controls();renderRepeat(); $('status').textContent='重新连接中';
        notice('暂时连接不到桥接，正在自动重连');
    } finally { updating=false; }
}
async function act(action) {
    if(pending || state.stale || !(state.actions || []).includes(action))return;
    pending=true; controls();
    try {
        const r=await fetch('/api/action', {method:'POST', headers:{'Content-Type':'application/json'},
            signal:AbortSignal.timeout(10000),
            body:JSON.stringify({action,track_id:state.track_id,request_id:crypto.randomUUID()})});
        if(r.status===401) { await reconnect(); notice('连接已恢复，请重新按一次'); return; }
        const v=await r.json(); notice(v.ok?(action==='repeat_one'?'已发送单曲循环指令':action==='repeat_all'?'已发送列表循环指令':'指令已发送，等待播放器更新'):v.error || '操作未完成');
    } catch(error) { notice('结果未知，请先检查播放器；未自动重试'); }
    finally { pending=false; controls(); await update(); }
}
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>act(b.dataset.action));
window.addEventListener('keydown',e=>{
    if(e.repeat||e.metaKey||e.ctrlKey||e.altKey||['INPUT','TEXTAREA'].includes(e.target.tagName))return;
    if(currentApp==='shelf'){handleShelfKey(e);return;}
    if(e.key==='Escape'){e.preventDefault();showApp('home');return;}
    if((currentApp==='home'||currentApp==='music'||currentApp==='radio')&&e.key.toLowerCase()==='s'){e.preventDefault();showApp('connection');return;}
    if(currentApp==='connection'){const k=e.key.toLowerCase();if(k==='q')showApp('home');const m={a:'auto',h:'home',w:'work',e:'edit'}[k];if(m)networkPreview(m);return;}
    if(currentApp==='home'){
        const app={m:'music',p:'shelf',r:'radio',a:'companion'}[e.key.toLowerCase()];
        if(app){e.preventDefault();pressHomeCard(app);}
        return;
    }
    if((currentApp==='companion'||currentApp==='radio')&&e.key.toLowerCase()==='q'){e.preventDefault();showApp('home');return;}
    if(currentApp==='radio'){
        const k=e.key.toLowerCase();
        if([' ','enter','j','k','r','1','2','3','+','-','='].includes(k))e.preventDefault();
        if(k===' '||k==='enter')toggleRadio();
        else if(k==='j')selectRadio(radioIndex-1);
        else if(k==='k'||k==='r')selectRadio(radioIndex+1);
        else if(['1','2','3'].includes(k))selectRadio(Number(k)-1);
        else if(k==='+'||k==='=')volumeRadio(8);
        else if(k==='-')volumeRadio(-8);
        return;
    }
    if(currentApp!=='music')return;
    if(e.key.toLowerCase()==='p'){e.preventDefault();openShelf();return;}
    if(e.key.toLowerCase()==='q'){e.preventDefault();showApp('home');return;}
    const action={' ':'toggle',j:'previous',k:'next',h:'favorite',c:'repeat_one',v:'repeat_all'}[e.key.toLowerCase()];
    if(action){e.preventDefault();act(action);}
});
$('pair').onclick=async()=>{
    try { const v=await (await read('/api/pair')).json(); $('pairtext').textContent='端口: '+v.port+'\n密钥: '+v.token; }
    catch(error) { notice('暂时无法读取配对信息'); }
};
// A recursive timer keeps polling after failures without overlapping requests.
(async function tick(){ await update(); void updatePower(); setTimeout(tick,500); })();

// Playlist selection is separate from playback state. Browsing sends GETs only.
const shelfSlots=[['shelfFarPrevious',-2],['shelfPrevious',-1],['shelfPlay',0],['shelfNext',1],['shelfFarNext',2]];
const shelfCanAnimate=typeof requestAnimationFrame==='function'&&typeof cancelAnimationFrame==='function';
const shelfReduceMotion=typeof matchMedia==='function'&&matchMedia('(prefers-reduced-motion: reduce)').matches;
const shelf = {items:[], selected:'', index:0, epoch:0, timer:0, loading:false, playing:false,
    message:'正在读取收藏歌单', error:false, attempts:0, imageTimers:new Set(),
    motion:{position:0,target:0,velocity:0,anchor:0,raf:0,last:0},
    drag:{active:false,moved:false,suppress:false,pointer:-1,startX:0,startPosition:0,lastX:0,lastAt:0,velocity:0}};
function shelfModulo(value,count){return ((value%count)+count)%count;}
function stopShelfMotion(){
    if(shelf.motion.raf&&shelfCanAnimate)cancelAnimationFrame(shelf.motion.raf);
    shelf.motion.raf=0;shelf.motion.last=0;shelf.motion.velocity=0;
    shelf.drag.active=false;
    const flow=$('shelfFlow');if(flow){flow.dataset.dragging='false';flow.dataset.moving='false';}
}
function stopShelf(){
    stopShelfMotion();
    shelf.epoch++;clearTimeout(shelf.timer);
    for(const timer of shelf.imageTimers)clearTimeout(timer);
    shelf.imageTimers.clear();
    for(const [id] of shelfSlots){
        const img=$(id).querySelector('img');if(img){img.onload=img.onerror=null;img.remove();}
        $(id).dataset.playlist='';
    }
}
function shelfCover(button, item){
    button.hidden=!item;
    button.disabled=shelf.playing;
    if(!item){button.dataset.playlist='';button.replaceChildren(document.createTextNode('♪'));return;}
    if(button.dataset.playlist===item.id)return;
    button.dataset.playlist=item.id;button.replaceChildren(document.createTextNode('♪'));
    if(!/^\/api\/playlist-artwork\/[0-9]+$/.test(item.artwork||''))return;
    const img=new Image();img.alt='';img.decoding='async';let attempts=0;
    const epoch=shelf.epoch,selectedId=item.id;
    img.onload=()=>{if(button.dataset.playlist===selectedId)img.style.opacity='1';};
    img.onerror=()=>{
        img.style.opacity='0';
        if(++attempts>=6||currentApp!=='shelf'||shelf.epoch!==epoch||button.dataset.playlist!==selectedId)return;
        const timer=setTimeout(()=>{
            shelf.imageTimers.delete(timer);
            if(currentApp==='shelf'&&shelf.epoch===epoch&&button.dataset.playlist===selectedId)
                img.src=item.artwork+'?retry='+attempts;
        },1000+attempts*500);
        shelf.imageTimers.add(timer);
    };
    img.src=item.artwork;button.append(img);
}
function syncShelfSlots(){
    const count=shelf.items.length,anchor=shelf.motion.anchor;
    for(const [id,relative] of shelfSlots){
        const button=$(id),item=count&&(count>1||relative===0)?shelf.items[shelfModulo(anchor+relative,count)]:null;
        shelfCover(button,item);
        if(!item)continue;
        if(relative===0)button.ariaLabel='播放 '+item.title+'，空格';
        else if(relative===-1)button.ariaLabel='上一张歌单 '+item.title+'，J';
        else if(relative===1)button.ariaLabel='下一张歌单 '+item.title+'，K';
    }
}
function updateShelfCopy(){
    const item=shelf.items[shelf.index],count=shelf.items.length;
    const moving=Boolean(shelf.motion.raf||shelf.drag.active||Math.abs(shelf.motion.target-shelf.motion.position)>.002);
    $('shelfCount').textContent=count?`${shelf.index+1} / ${count}`:'';
    $('shelfPlay').disabled=shelf.playing||shelf.loading||shelf.error||moving;
    $('shelfControlPrevious').disabled=shelf.playing||moving||count<2;
    $('shelfControlNext').disabled=shelf.playing||moving||count<2;
    $('shelfControlPlay').disabled=shelf.playing||shelf.loading||shelf.error||moving||!item;
    $('shelfTitle').textContent=shelf.playing?'正在请求播放…':shelf.error&&item?shelf.message:item?.title||'';
    $('shelfTitle').title=item?`${item.title} · ${item.creator||''} · ${item.track_count||0} 首`:'';
    $('shelfPlay').ariaLabel=item?'播放 '+item.title+'，空格':'播放选中的歌单，空格';
    $('shelfMessage').hidden=Boolean(item);
    $('shelfMessage').textContent=shelf.message;
    $('shelfRetry').hidden=Boolean(item)||shelf.loading||shelf.playing;
    $('shelfRefresh').disabled=shelf.loading||shelf.playing;
    $('shelfRefresh').dataset.loading=String(shelf.loading);
    $('shelfRefresh').ariaLabel=shelf.loading?'正在读取收藏':shelf.error?'读取失败，按 R 重试':'刷新收藏歌单，R';
    $('shelfRefresh').title=shelf.loading?'正在读取收藏':shelf.error?'读取失败，按 R 重试':'刷新收藏歌单，R';
    $('shelfStatus').textContent=shelf.playing?'正在请求播放':shelf.message;
}
function applyShelfMotion(){
    const count=shelf.items.length,flow=$('shelfFlow'),motion=shelf.motion;
    while(motion.position>=motion.anchor+1-.0001)motion.anchor++;
    while(motion.position<=motion.anchor-1+.0001)motion.anchor--;
    syncShelfSlots();
    if(count){
        const focus=shelfModulo(Math.round(motion.position),count);
        if(focus!==shelf.index){shelf.index=focus;shelf.selected=shelf.items[focus].id;}
    }
    const moving=Boolean(motion.raf||shelf.drag.active||Math.abs(motion.target-motion.position)>.002);
    if(flow){flow.dataset.moving=String(moving);flow.dataset.dragging=String(shelf.drag.active);}
    for(const [id,relative] of shelfSlots){
        const button=$(id),offset=relative+motion.anchor-motion.position,absolute=Math.abs(offset);
        const distance=absolute<=1?84*absolute:84+44*(absolute-1);
        const x=Math.sign(offset)*distance;
        const y=7*Math.min(absolute,2.2);
        const scale=Math.max(.64,.94-.13*Math.min(absolute,2.3));
        const angleStep=35*Math.min(absolute,1)+20*Math.max(0,Math.min(absolute-1,1));
        const angle=-Math.sign(offset)*angleStep;
        const z=-72*Math.min(absolute,2.5);
        const opacity=Math.max(0,1-.19*Math.min(absolute,1)-.31*Math.max(0,absolute-1));
        button.style.transform=`translate3d(${x.toFixed(2)}px,${y.toFixed(2)}px,${z.toFixed(2)}px) rotateY(${angle.toFixed(2)}deg) scale(${scale.toFixed(3)})`;
        button.style.opacity=String(opacity);button.style.zIndex=String(1000-Math.round(absolute*100));
        const prominence=Math.max(0,1-Math.min(absolute,1));
        const shadowY=2+3*prominence;
        const shadowBlur=6+6*prominence;
        const shadowAlpha=.24+.18*prominence;
        button.style.boxShadow=`0 ${shadowY.toFixed(1)}px ${shadowBlur.toFixed(1)}px rgba(0,0,0,${shadowAlpha.toFixed(2)})`;
        button.style.setProperty?.('--edge-shade',String(Math.min(.72,absolute*.62)));
        button.disabled=shelf.playing||moving||(id==='shelfPlay'&&(shelf.loading||shelf.error));
    }
    updateShelfCopy();
}
function settleShelfMotion(){
    const motion=shelf.motion;
    motion.position=motion.target;motion.velocity=0;motion.raf=0;motion.last=0;
    applyShelfMotion();
}
function shelfMotionFrame(now){
    const motion=shelf.motion;
    if(currentApp!=='shelf'){stopShelfMotion();return;}
    const dt=motion.last?Math.min(.032,(now-motion.last)/1000):1/60;motion.last=now;
    const error=motion.target-motion.position;
    motion.velocity+=(error*185-motion.velocity*24)*dt;
    motion.position+=motion.velocity*dt;
    if(Math.abs(error)<.0015&&Math.abs(motion.velocity)<.018){settleShelfMotion();return;}
    applyShelfMotion();motion.raf=requestAnimationFrame(shelfMotionFrame);
}
function startShelfMotion(){
    if(shelfReduceMotion||!shelfCanAnimate){settleShelfMotion();return;}
    if(shelf.motion.raf)return;
    shelf.motion.last=0;shelf.motion.raf=requestAnimationFrame(shelfMotionFrame);applyShelfMotion();
}
function renderShelf(){
    const view=document.querySelector('.shelf-view');
    view.dataset.busy=String(shelf.playing);view.setAttribute('aria-busy',String(shelf.loading||shelf.playing));
    applyShelfMotion();
}
async function loadShelf(refresh=false){
    const epoch=shelf.epoch;shelf.loading=true;shelf.error=false;renderShelf();
    try{
        const value=await (await read('/api/playlists'+(refresh?'?refresh=1':''))).json();
        if(currentApp!=='shelf'||epoch!==shelf.epoch)return;
        if(value.ok&&Array.isArray(value.items)){
            const items=value.items.filter(x=>x&&typeof x.id==='string'&&/^\d+$/.test(x.id)&&typeof x.title==='string');
            shelf.items=items;shelf.index=Math.max(0,items.findIndex(x=>x.id===shelf.selected));
            shelf.selected=items[shelf.index]?.id||'';
            stopShelfMotion();shelf.motion.position=shelf.index;shelf.motion.target=shelf.index;shelf.motion.anchor=shelf.index;
            shelf.loading=Boolean(value.loading);shelf.error=Boolean(value.stale);
            shelf.message=value.message||(items.length?'只显示收藏的歌单':'还没有收藏的歌单\n在网易云收藏后，按 R 刷新');
        }else{
            shelf.loading=Boolean(value.loading);shelf.error=!shelf.loading;
            shelf.message=value.message||'暂时读不到收藏歌单，请按 R 重试';
        }
        if(shelf.loading){
            if(++shelf.attempts<75)shelf.timer=setTimeout(()=>loadShelf(),800);
            else{shelf.loading=false;shelf.error=true;shelf.message='读取较慢，请稍后按 R 重试';}
        }
    }catch(error){
        if(currentApp!=='shelf'||epoch!==shelf.epoch)return;
        shelf.loading=false;shelf.error=true;shelf.message='连接不到 Mac，请检查桥接后按 R 重试';
    }
    if(currentApp==='shelf'&&epoch===shelf.epoch)renderShelf();
}
function openShelf(){
    if(currentApp!=='music'&&currentApp!=='home')return;
    shelf.epoch++;shelf.attempts=0;shelf.message='正在读取收藏歌单';
    stopShelfMotion();shelf.motion.position=shelf.index;shelf.motion.target=shelf.index;shelf.motion.anchor=shelf.index;
    showApp('shelf');renderShelf();void loadShelf();
}
function moveShelf(delta){
    if(shelf.playing||!shelf.items.length)return;
    const direction=Math.sign(delta);if(!direction||shelf.items.length<2)return;
    if(!shelfCanAnimate||shelfReduceMotion){
        const next=shelfModulo(shelf.index+direction,shelf.items.length);
        shelf.index=next;shelf.selected=shelf.items[next].id;
        shelf.motion.position+=direction;shelf.motion.target=shelf.motion.position;shelf.motion.anchor=shelf.motion.position;
        renderShelf();return;
    }
    const currentDirection=Math.sign(shelf.motion.target-shelf.motion.position)||Math.sign(shelf.motion.velocity);
    shelf.motion.target=currentDirection&&currentDirection!==direction?Math.round(shelf.motion.position)+direction:shelf.motion.target+direction;
    if(Math.abs(shelf.motion.target-shelf.motion.position)>4)
        shelf.motion.target=shelf.motion.position+direction*4;
    startShelfMotion();
}
function refreshShelf(){
    if(shelf.loading||shelf.playing)return;
    stopShelfMotion();shelf.motion.position=Math.round(shelf.motion.position);shelf.motion.target=shelf.motion.position;shelf.motion.anchor=shelf.motion.position;
    clearTimeout(shelf.timer);shelf.attempts=0;void loadShelf(true);
}
async function playShelf(){
    const moving=shelf.motion.raf||shelf.drag.active||Math.abs(shelf.motion.target-shelf.motion.position)>.002;
    const item=shelf.items[shelf.index];if(!item||shelf.playing||shelf.loading||shelf.error||moving)return;
    shelf.playing=true;renderShelf();const epoch=shelf.epoch;
    let resultMessage='';
    try{
        const response=await fetch('/api/playlists/play',{method:'POST',headers:{'Content-Type':'application/json'},
            body:JSON.stringify({id:item.id,request_id:crypto.randomUUID()}),signal:AbortSignal.timeout(25000)});
        if(response.status===401){await reconnect();resultMessage='连接已恢复，请重新确认播放';}
        else{
            const value=await response.json();
            if(value.ok){
                if(currentApp==='shelf'&&epoch===shelf.epoch){showApp('music');notice('已请求播放歌单，等待网易云更新');}
            }else resultMessage=value.message||value.error||'未能播放，请检查 Mac 网易云';
        }
    }catch(error){resultMessage='播放结果未知，请先检查 Mac；没有自动重试';}
    finally{
        shelf.playing=false;
        if(currentApp==='shelf'&&epoch===shelf.epoch){
            if(resultMessage){shelf.message=resultMessage;$('shelfTitle').textContent=resultMessage;}
            renderShelf();
            if(resultMessage){$('shelfTitle').textContent=resultMessage;$('shelfTitle').title=resultMessage;}
        }
    }
}
function handleShelfKey(e){
    const key=e.key.toLowerCase();
    if(['escape','q','j','k','arrowleft','arrowright',' ','enter','r'].includes(key))e.preventDefault();
    if(key==='q'||key==='escape')showApp('home');
    else if(key==='j'||key==='arrowleft')moveShelf(-1);
    else if(key==='k'||key==='arrowright')moveShelf(1);
    else if(key===' '||key==='enter')void playShelf();
    else if(key==='r')refreshShelf();
}
$('openShelf').onclick=openShelf;
$('shelfBack').onclick=()=>showApp('home');
$('shelfPrevious').onclick=()=>moveShelf(-1);$('shelfNext').onclick=()=>moveShelf(1);
$('shelfPlay').onclick=()=>void playShelf();$('shelfRetry').onclick=$('shelfRefresh').onclick=refreshShelf;
$('shelfControlPrevious').onclick=()=>moveShelf(-1);$('shelfControlNext').onclick=()=>moveShelf(1);
$('shelfControlPlay').onclick=()=>void playShelf();
const shelfFlow=$('shelfFlow');
if(shelfFlow&&typeof shelfFlow.addEventListener==='function'){
    const finishShelfDrag=e=>{
        if(!shelf.drag.active||e.pointerId!==shelf.drag.pointer)return;
        shelf.drag.active=false;shelfFlow.dataset.dragging='false';
        if(shelfFlow.hasPointerCapture?.(e.pointerId))shelfFlow.releasePointerCapture(e.pointerId);
        if(shelf.drag.moved){
            const projected=shelf.motion.position+shelf.drag.velocity*.12;
            const minimum=shelf.drag.startPosition-2,maximum=shelf.drag.startPosition+2;
            shelf.motion.target=Math.max(minimum,Math.min(maximum,Math.round(projected)));
            shelf.motion.velocity=shelf.drag.velocity;
            shelf.drag.suppress=true;
            startShelfMotion();
        }else if(shelf.drag.wasMoving){
            // A tap during animation must not freeze a fractional pose or play it.
            shelf.drag.suppress=true;shelf.motion.target=shelf.drag.resumeTarget;startShelfMotion();
        }else applyShelfMotion();
        if(e.type==='pointercancel')shelf.drag.suppress=true;
    };
    const beginShelfDrag=e=>{
        if(shelf.drag.active||(e.pointerType==='mouse'&&e.button!==0))return;
        // Suppress only the preceding drag's click, never a new independent tap.
        shelf.drag.suppress=false;
        if(shelf.playing||shelf.loading||shelf.items.length<2)return;
        shelf.drag.wasMoving=Boolean(shelf.motion.raf||Math.abs(shelf.motion.target-shelf.motion.position)>.002);
        shelf.drag.resumeTarget=shelf.motion.target;
        stopShelfMotion();
        shelf.drag.active=true;shelf.drag.moved=false;shelf.drag.pointer=e.pointerId;
        shelf.drag.startX=shelf.drag.lastX=e.clientX;shelf.drag.startPosition=shelf.motion.position;
        shelf.drag.lastAt=performance.now();shelf.drag.velocity=0;shelf.motion.target=shelf.motion.position;
        shelfFlow.dataset.dragging='true';
        // Leave a tap targeted at its cover. Capturing here retargets click to the track.
    };
    const moveShelfDrag=e=>{
        if(!shelf.drag.active||e.pointerId!==shelf.drag.pointer)return;
        const dx=e.clientX-shelf.drag.startX;if(!shelf.drag.moved&&Math.abs(dx)<5)return;
        if(!shelf.drag.moved)shelfFlow.setPointerCapture?.(e.pointerId);
        shelf.drag.moved=true;e.preventDefault();
        const now=performance.now(),dt=Math.max(.008,(now-shelf.drag.lastAt)/1000);
        shelf.drag.velocity=(shelf.drag.lastX-e.clientX)/84/dt;
        shelf.drag.lastX=e.clientX;shelf.drag.lastAt=now;
        const dragPosition=shelf.drag.startPosition-dx/84;
        shelf.motion.position=Math.max(shelf.drag.startPosition-2.15,Math.min(shelf.drag.startPosition+2.15,dragPosition));
        shelf.motion.target=shelf.motion.position;applyShelfMotion();
    };
    shelfFlow.addEventListener('pointerdown',beginShelfDrag);
    shelfFlow.addEventListener('pointermove',moveShelfDrag);
    // The 3D track cannot be a hit-test plane: it obscures cards behind z=0.
    // Its empty areas hit the outer view instead, which keeps gap swipes working.
    const shelfView=document.querySelector('.shelf-view');
    shelfView.addEventListener('pointerdown',e=>{
        if(e.target!==shelfView)return;
        const bounds=shelfFlow.getBoundingClientRect();
        if(e.clientX>=bounds.left&&e.clientX<=bounds.right&&e.clientY>=bounds.top&&e.clientY<=bounds.bottom)beginShelfDrag(e);
    });
    shelfView.addEventListener('pointermove',e=>{if(e.target===shelfView)moveShelfDrag(e);});
    shelfFlow.addEventListener('pointerup',finishShelfDrag);
    shelfFlow.addEventListener('pointercancel',finishShelfDrag);
    // An uncaptured tap may end outside the track; never leave the gesture active.
    if(typeof window!=='undefined'){
        window.addEventListener('pointerup',finishShelfDrag);
        window.addEventListener('pointercancel',finishShelfDrag);
    }
    shelfFlow.addEventListener('click',e=>{
        if(!shelf.drag.suppress)return;
        shelf.drag.suppress=false;e.preventDefault();e.stopPropagation();
    },true);
}
if(currentApp==='shelf'){renderShelf();void loadShelf();}
