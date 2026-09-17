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
const homeCards={music:'openMusic',radio:'openRadio',companion:'openCompanion'};
function pressHomeCard(app){
    if(currentApp!=='home'||homeLaunchTarget)return;
    homeLaunchTarget=app;
    $(homeCards[app]).dataset.pressed='true';
    // Let a quick tap paint the full press before navigating. No player command.
    homeLaunchTimer=setTimeout(()=>showApp(app),160);
}
function showApp(app) {
    clearTimeout(homeLaunchTimer);homeLaunchTimer=0;homeLaunchTarget='';
    Object.values(homeCards).forEach(id=>{$(id).dataset.pressed='false'});
    currentApp=['music','radio','companion','connection'].includes(app)?app:'home';
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
$('openMusic').onclick=()=>pressHomeCard('music');$('openRadio').onclick=()=>pressHomeCard('radio');
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
    if(!['home','music','radio'].includes(currentApp)||powerPending||Date.now()-powerPollAt<10000)return;
    powerPollAt=Date.now();powerPending=true;
    try{
        const {device}=await (await read('/api/power')).json();
        const p=device?.power,age=Date.now()-(device?.received_at||0)*1000;
        const valid=p?.valid&&age>=0&&age<120000;
        const percent=valid&&p.battery_present&&Number.isInteger(p.percent)?p.percent:null;
        const charging=valid&&p.battery_present&&p.charging;
        for(const prefix of ['homePower','musicPower','radioPower']){
        $(prefix+'Percent').textContent=percent===null?'--':percent+'%';
        $(prefix+'Fill').style.width=(charging?20:percent===null?0:20*Math.max(0,Math.min(100,percent))/100)+'px';
        const detail=!valid?'电量待同步':!p.battery_present?'未检测到电池':charging?'充电中':p.usb_present?'USB 供电':'电池供电';
        $(prefix).title=detail;$(prefix).ariaLabel=$(prefix+'Percent').textContent+' · '+detail;
        $(prefix).dataset.state=charging?'charging':percent!==null&&percent<=15?'critical':percent!==null&&percent<=30?'low':'normal';
        }
    }catch(error){for(const prefix of ['homePower','musicPower','radioPower']){$(prefix+'Percent').textContent='--';$(prefix+'Fill').style.width='0px';$(prefix).title='电量待同步';$(prefix).ariaLabel='电量待同步';$(prefix).dataset.state='normal';}}
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
    if(e.key==='Escape'){e.preventDefault();showApp('home');return;}
    if((currentApp==='home'||currentApp==='music'||currentApp==='radio')&&e.key.toLowerCase()==='s'){e.preventDefault();showApp('connection');return;}
    if(currentApp==='connection'){const k=e.key.toLowerCase();if(k==='q')showApp('home');const m={a:'auto',h:'home',w:'work',e:'edit'}[k];if(m)networkPreview(m);return;}
    if(currentApp==='home'){if(e.key.toLowerCase()==='m')pressHomeCard('music');if(e.key.toLowerCase()==='r')pressHomeCard('radio');if(e.key.toLowerCase()==='a')pressHomeCard('companion');return;}
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
