const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
function harness({animate=false}={}){
  class Element{
    constructor(){this.dataset={};this.children=[];this.style={};this.hidden=false;this.disabled=false;this.listeners={};this.captured=null;}
    replaceChildren(...children){this.children=children;}
    append(child){this.children.push(child);}
    querySelector(){return this.children.find(x=>x instanceof Img)||null;}
    setAttribute(k,v){this[k]=v;}
    getBoundingClientRect(){return {left:0,right:320,top:40,bottom:172};}
    addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);}
    setPointerCapture(id){this.captured=id;}
    hasPointerCapture(id){return this.captured===id;}
    releasePointerCapture(id){if(this.captured===id)this.captured=null;}
    emit(type,extra={}){const e={type,pointerId:1,pointerType:'mouse',button:0,clientX:160,preventDefault(){this.prevented=true},stopPropagation(){this.stopped=true},...extra};for(const fn of this.listeners[type]||[])fn(e);return e;}
  }
  class Img extends Element{remove(){}}
  const elements=new Map(),posts=[],views=[],timers=[],frames=new Map();let frameId=0,now=0;
  const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
  const items=[{id:'1',title:'First',artwork:'/api/playlist-artwork/1'},
    {id:'2',title:'Second',artwork:'/api/playlist-artwork/2'},
    {id:'3',title:'Third',artwork:'/api/playlist-artwork/3'}];
  let response={ok:true,items},postResponse=Promise.resolve({ok:true});
  const context=vm.createContext({$:get,document:{querySelector:get,createTextNode:s=>s},Image:Img,
    currentApp:'music',window:get('window'),performance:{now:()=>now},setTimeout:fn=>{timers.push(fn);return timers.length},clearTimeout:()=>{},
    ...(animate?{requestAnimationFrame:fn=>{frames.set(++frameId,fn);return frameId},
      cancelAnimationFrame:id=>frames.delete(id),matchMedia:()=>({matches:false})}:{}),
    read:async()=>({json:async()=>response}),
    fetch:async(url,options)=>{posts.push({url,body:JSON.parse(options.body)});return {status:200,json:()=>postResponse}},
    crypto:{randomUUID:()=> 'request-unique'},AbortSignal:{timeout:()=>null},notice:()=>{},reconnect:async()=>{},
  });
  context.showApp=app=>{if(context.currentApp==='shelf'&&app!=='shelf')vm.runInContext('stopShelf()',context);context.currentApp=app;views.push(app)};
  vm.runInContext(fs.readFileSync('music_bridge/client.js','utf8').split('// Playlist selection is separate from playback state. Browsing sends GETs only.')[1],context);
  return {context,get,posts,views,items,timers,run:s=>vm.runInContext(s,context),
    response:v=>response=v,postResponse:v=>postResponse=v,
    gesture(id,{dx=0,click=true}={}){
      const flow=get('shelfFlow'),target=get(id);flow.emit('pointerdown',{target});
      if(dx)flow.emit('pointermove',{target,clientX:160+dx});
      // Browser click targets pointer-up capture even when released inside pointerup.
      const clickTarget=flow.hasPointerCapture(1)?flow:target;
      flow.emit('pointerup',{target:clickTarget,clientX:160+dx});
      if(click){const event=flow.emit('click',{target:clickTarget});if(!event.stopped&&!clickTarget.disabled)clickTarget.onclick?.();}
    },
    frame(ms=16.667){now+=ms;const pending=[...frames.values()];frames.clear();for(const fn of pending)fn(now);},
    settleFrames(limit=240){for(let i=0;i<limit&&frames.size;i++)this.frame();assert.equal(frames.size,0,'motion should settle');}};
}
const settle=()=>new Promise(resolve=>setImmediate(resolve));
test('Music exposes Now Playing and Collection as sibling routes',()=>{
 const html=fs.readFileSync('music_bridge/preview.html','utf8');
 const refreshSymbol=html.match(/<symbol id="paper-refresh".*?<\/symbol>/s)?.[0];
 assert.match(html,/id="musicCard"[^>]*>.*id="openMusic"[^>]*>.*id="openShelfHome"/s);
 assert.match(html,/\.launcher-view #openMusic\{left:0;width:184px/);
 assert.match(html,/\.launcher-view #openShelfHome\{left:184px;width:120px/);
 assert.doesNotMatch(html,/\.launcher-view \.music-card::after\{/);
 assert.match(html,/\.home-record-vinyl\{[^}]*left:8px;top:0;width:104px;height:92px;[^}]*clip-path:circle\(52px at 52px 46px\)/);
 assert.doesNotMatch(html,/\.home-record-vinyl\{[^}]*clip-path:inset/);
 assert.match(html,/\.home-collection-sleeve\{[^}]*left:2px;top:64px;width:116px;height:26px/);
 assert.match(html,/id="openShelfHome"[^>]*>.*home-record-vinyl.*home-collection-sleeve.*COLLECTION.*<span>P<\/span>/s);
 assert.doesNotMatch(html,/home-collection-footer/);
 assert.doesNotMatch(html,/home-shelf-badge/);
 assert.match(html,/class="meta-actions">.*id="heart".*id="openShelf"[^>]*>.*#paper-collection.*<span>P<\/span>/s);
 assert.match(html,/\.meta-actions>button\{[^}]*width:50px;height:20px;[^}]*border-radius:999px;[^}]*background:#f4e7d38c/);
 assert.doesNotMatch(html,/class="top">.*id="openShelf".*class="music-indicators"/s);
 assert.match(html,/<footer class="shelf-foot"[^>]*>.*id="shelfRefresh".*<span>R<\/span>.*id="shelfControlPrevious".*id="shelfControlPlay".*id="shelfControlNext"/s);
 assert.ok(refreshSymbol,'refresh icon symbol should exist');
 assert.match(refreshSymbol,/<path d="M244,56v48/);
 assert.doesNotMatch(refreshSymbol,/stroke-width|fill="none"/);
 assert.doesNotMatch(html,/class="shelf-indicators"[^>]*>.*id="shelfRefresh".*id="shelfPower"/s);
 assert.match(html,/id="shelfBack" aria-label="返回首页，Q"/);
 assert.doesNotMatch(html,/P 歌单|返回音乐，Q/);
});
test('yellow shelf halo stays outside the 3D cover scene',()=>{
 const html=fs.readFileSync('music_bridge/preview.html','utf8');
 assert.match(html,/\.shelf-view::before\{[^}]*background:#ffd21f;[^}]*z-index:0\}/);
 assert.doesNotMatch(html,/\.shelf-flow::before\{/);
 assert.match(html,/\.shelf-flow\{[^}]*z-index:1(?:;[^}]*)?\}/);
});
test('opening, navigating, and returning do not send playback; reopens at last selection',async()=>{
 const h=harness();h.run('openShelf()');await settle();
 assert.equal(h.get('shelfTitle').textContent,'First');
 h.get('shelfControlNext').onclick();assert.equal(h.get('shelfTitle').textContent,'Second');assert.equal(h.posts.length,0);
 h.run("handleShelfKey({key:'Escape',preventDefault(){}})");assert.equal(h.context.currentApp,'home');
 h.run('openShelf()');await settle();assert.equal(h.get('shelfTitle').textContent,'Second');assert.equal(h.posts.length,0);
});
test('play is sent once while in flight and late success cannot navigate away from another view',async()=>{
 const h=harness();h.run('openShelf()');await settle();let resolve;
 h.postResponse(new Promise(r=>resolve=r));h.run('playShelf()');h.run('playShelf()');await settle();
 assert.equal(h.posts.length,1);assert.equal(h.posts[0].body.id,'1');
 h.context.showApp('music');h.context.showApp('home');resolve({ok:true});await settle();
 assert.equal(h.context.currentApp,'home');
});
test('loading and failed reads disable playback; retry can recover',async()=>{
 const h=harness();h.response({ok:false,loading:true,message:'Loading'});h.run('openShelf()');await settle();
 h.run('playShelf()');assert.equal(h.posts.length,0);assert.equal(h.get('shelfRetry').hidden,true);
 h.response({ok:false,loading:false,message:'Login needed'});await h.run('loadShelf()');
 assert.equal(h.get('shelfRetry').hidden,false);assert.equal(h.get('shelfMessage').textContent,'Login needed');
 h.response({ok:true,items:h.items});h.run('refreshShelf()');await settle();assert.equal(h.get('shelfTitle').textContent,'First');
});
test('expired session reconnects without replaying action',async()=>{
 const h=harness();h.run('openShelf()');await settle();let calls=0;
 h.context.fetch=async()=>{calls++;return {status:401}};await h.run('playShelf()');
 assert.equal(calls,1);assert.equal(h.context.currentApp,'shelf');assert.match(h.get('shelfTitle').textContent,/重新确认/);
});
test('stale library remains browsable but cannot issue play until refreshed',async()=>{
 const h=harness();h.response({ok:true,items:h.items,stale:true,message:'Refresh failed'});h.run('openShelf()');await settle();
 assert.equal(h.get('shelfPlay').disabled,true);h.run('moveShelf(1)');h.run('playShelf()');
 assert.equal(h.get('shelfCount').textContent,'2 / 3');assert.equal(h.posts.length,0);
 h.response({ok:true,items:h.items});h.run('refreshShelf()');await settle();
 assert.equal(h.get('shelfTitle').textContent,'Second');assert.equal(h.get('shelfPlay').disabled,false);
});
test('cover errors have a finite retry budget and cannot load arbitrary URLs',async()=>{
 const h=harness();h.run('openShelf()');await settle();const img=h.get('shelfPlay').querySelector('img');
 for(let i=0;i<6;i++)img.onerror();assert.equal(h.timers.length,5);
 h.items[1].artwork='https://example.com/untrusted';h.run('moveShelf(1)');
 assert.equal(h.get('shelfPlay').querySelector('img'),null);
});
test('shelf wraps at either end and hides neighbours for a single record',async()=>{
 const h=harness();h.run('openShelf()');await settle();h.run('moveShelf(-1)');
 assert.equal(h.get('shelfTitle').textContent,'Third');h.run('moveShelf(1)');assert.equal(h.get('shelfTitle').textContent,'First');
 h.response({ok:true,items:[h.items[0]]});await h.run('loadShelf()');
 assert.equal(h.get('shelfPrevious').hidden,true);assert.equal(h.get('shelfNext').hidden,true);
});
test('animated shelf keeps transforms continuous, accepts repeated input, and reverses in flight',async()=>{
 const h=harness({animate:true});h.run('openShelf()');await settle();
 assert.match(h.get('shelfPlay').style.transform,/scale\(0\.940\)/);
 assert.match(h.get('shelfPrevious').style.transform,/rotateY\(35\.00deg\) scale\(0\.810\)/);
 h.run('moveShelf(1)');h.frame();h.frame();h.frame();
 const moving=h.run('shelf.motion.position');assert.ok(moving>0&&moving<1);
 assert.match(h.get('shelfPlay').style.transform,/rotateY/);assert.equal(h.posts.length,0);
 h.run('moveShelf(-1)');h.settleFrames();
 assert.equal(h.run('shelf.index'),2);assert.equal(h.get('shelfTitle').textContent,'Third');
 h.run('moveShelf(1);moveShelf(1)');h.settleFrames();
 assert.equal(h.run('shelf.index'),1);assert.equal(h.get('shelfTitle').textContent,'Second');
 assert.equal(h.posts.length,0);
});
test('stationary central cover click reaches playback; side cover only browses',async()=>{
 const h=harness();h.run('openShelf()');await settle();
 h.gesture('shelfNext');assert.equal(h.run('shelf.index'),1);assert.equal(h.posts.length,0);
 h.gesture('shelfPlay');await settle();assert.equal(h.posts.length,1);assert.equal(h.posts[0].body.id,'2');
});
test('drag suppresses release click but a fresh central tap plays, even if drag emitted no click',async()=>{
 for(const click of [true,false]){
  const h=harness();h.run('openShelf()');await settle();h.gesture('shelfPlay',{dx:-90,click});
  assert.equal(h.posts.length,0);h.gesture('shelfPlay');await settle();assert.equal(h.posts.length,1);
 }
});
test('tap during motion cannot play or strand a fractional pose; settled tap and bottom play still work',async()=>{
 const h=harness({animate:true});h.run('openShelf()');await settle();
 h.run('moveShelf(1)');h.frame();h.frame();h.gesture('shelfPlay');assert.equal(h.posts.length,0);
 h.settleFrames();assert.equal(h.run('shelf.motion.position'),1);
 h.gesture('shelfPlay');await settle();assert.equal(h.posts.length,1);
 const b=harness();b.run('openShelf()');await settle();b.get('shelfControlPlay').onclick();await settle();assert.equal(b.posts.length,1);
});

test('uncaptured pointer released outside track does not leave the shelf stuck',async()=>{
 const h=harness();h.run('openShelf()');await settle();
 h.get('shelfFlow').emit('pointerdown');h.get('window').emit('pointerup');
 assert.equal(h.run('shelf.drag.active'),false);h.gesture('shelfPlay');await settle();assert.equal(h.posts.length,1);
});

test('swipe starting in the empty track area still browses without playing',async()=>{
 const h=harness();h.run('openShelf()');await settle();const view=h.get('.shelf-view');
 view.emit('pointerdown',{target:view,clientX:160,clientY:100});view.emit('pointermove',{target:view,clientX:70,clientY:100});
 h.get('shelfFlow').emit('pointerup');assert.equal(h.posts.length,0);assert.notEqual(h.run('shelf.index'),0);
});
