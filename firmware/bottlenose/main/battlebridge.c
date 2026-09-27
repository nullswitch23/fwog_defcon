/* BattleBridge: four-browser arena racer hosted wholly on Bottlenose.
 *
 * Browsers render at 30+ FPS. This module owns the authoritative 30 Hz
 * simulation and sends compact binary snapshots over WebSockets. UART to the
 * OG carries lobby/status only; it is deliberately not in the game loop.
 */
#include "battlebridge.h"
#include "battlebridge_game.h"
#include "pass_nvs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_coexist.h"
#include "lwip/sockets.h"

#define BB_SSID "FWOG-arena"
#define BB_TICK_MS 33u

typedef struct __attribute__((packed)) {
    uint8_t type, slot, phase, players;
    uint16_t tick, phase_ticks, arena_w, arena_h;
    uint8_t enemies, shots, gate_goal, pad;
} net_head_t;

typedef struct __attribute__((packed)) {
    uint8_t active, flags;
    uint16_t x, y;
    int8_t aim_x, aim_y;
    uint8_t hp;
    int16_t score;
    uint8_t gates, kills;
    uint16_t gate_x, gate_y;
    uint8_t color, shield, weap, gate_c;
} net_player_t;

typedef struct __attribute__((packed)) {
    uint8_t active, hp;
    uint16_t x, y;
    uint8_t target, pad;
} net_enemy_t;

typedef struct __attribute__((packed)) {
    uint8_t active, hostile;
    uint16_t x, y;
    int8_t vx, vy;
    uint8_t owner, kind;
} net_shot_t;

typedef struct __attribute__((packed)) {
    uint16_t x, y, w, h;
} net_wall_t;

typedef struct __attribute__((packed)) {
    uint8_t active, kind;
    uint16_t x, y;
} net_pickup_t;

typedef struct __attribute__((packed)) {
    net_head_t h;
    net_player_t p[BB_PLAYERS];
    net_enemy_t e[BB_ENEMIES];
    net_shot_t shot[BB_PROJECTILES];
    net_wall_t wall[BB_WALLS];
    net_pickup_t pk[BB_PICKUPS];
} net_snapshot_t;

_Static_assert(sizeof(net_head_t) == 16, "net_head_t");
_Static_assert(sizeof(net_player_t) == 21, "net_player_t");
_Static_assert(sizeof(net_enemy_t) == 8, "net_enemy_t");
_Static_assert(sizeof(net_shot_t) == 10, "net_shot_t");
_Static_assert(sizeof(net_wall_t) == 8, "net_wall_t");
_Static_assert(sizeof(net_pickup_t) == 6, "net_pickup_t");
_Static_assert(sizeof(net_snapshot_t) == 904, "net_snapshot_t");

static void (*s_puts)(const char *);
static bb_game_t s_game;
static SemaphoreHandle_t s_mu;
static httpd_handle_t s_httpd;
static TaskHandle_t s_task;
static int s_fd[BB_PLAYERS] = { -1, -1, -1, -1 };
static char s_pass[9];
static bool s_on;
static unsigned s_bots;
static bool s_teams;
static volatile uint32_t s_step_peak_us;

extern const uint8_t cover_jpg_start[] asm("_binary_cover_jpg_start");
extern const uint8_t cover_jpg_end[]   asm("_binary_cover_jpg_end");

static net_snapshot_t s_snap[BB_PLAYERS];
static volatile uint8_t s_snap_busy[BB_PLAYERS];

static const char k_index[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1,user-scalable=no'>"
"<title>FWOG BattleBridge</title><style>"
"*{box-sizing:border-box}html,body{margin:0;overflow:hidden;background:#060914;"
"color:#dce8ff;font:14px system-ui}canvas{display:block;width:100vw;height:100vh;"
"touch-action:none}#hud{position:fixed;left:10px;top:8px;text-shadow:0 1px #000}"
"#join{position:fixed;inset:0;display:grid;place-items:center;background:#060914;"
"overflow:auto;padding:12px}#join .card{text-align:center;max-width:640px}"
"#join img{width:100%;max-width:640px;border-radius:.7em;border:2px solid #ffd21a;"
"box-shadow:0 0 24px #ffd21a44}#join h1{margin:.45em 0 .2em;letter-spacing:.08em}"
"#go{position:fixed;inset:0;display:none;place-items:center;background:#000a;z-index:4}"
"#go .box{background:#10192c;border:2px solid #ffd21a;border-radius:.7em;padding:1.1em 1.5em;min-width:280px}"
"button{font:700 20px system-ui;padding:.8em 1.2em;border:0;border-radius:.5em;"
"background:#50c878;color:#07120a}#again{margin-top:.8em;width:100%}"
"#again:disabled{background:#2a3a4a;color:#789}small{display:block;color:#9ab;margin-top:.6em}"
"</style></head><body><canvas id=c width=960 height=600></canvas>"
"<div id=hud>connecting</div><div id=join><div class=card>"
"<img src=/cover.jpg alt='BattleBridge arena racer'>"
"<h1>BattleBridge</h1>"
"<button id=ready>READY</button><small>WASD move · mouse aim · left-click fire<br>"
"touch: left stick moves, right stick aims/fires<br>"
"green health · cyan shield · gold laser · orange spray · magenta grip<br>"
"3-kill streak upgrades the pea · rematch from GAME OVER</small></div></div>"
"<div id=go><div class=box><h2>GAME OVER</h2><div id=gl></div>"
"<button id=again>REMATCH</button></div></div><script>"
"const C=document.getElementById('c'),X=C.getContext('2d'),H=document.getElementById('hud'),"
"J=document.getElementById('join'),GO=document.getElementById('go'),GL=document.getElementById('gl'),"
"K={},P=new Map(),V={p:[],e:[],s:[]};let slot=255,S=null,ready=0,seq=0,AC=null,prev=null,lf=0,goWait=0;"
"const col=['#3cff7a','#ffd21a','#3ca8ff','#ff3a4a'];"
"const gcol=['#3cffea','#ff4ad6','#ff9a1a','#b4ff3c','#ff6ea8','#f4f4ff'];"
"const pkcol=['#3cff7a','#3ceaff','#ffd21a','#ff9a1a','#ff6bff'],wn=['PEA','LASER','SPRAY'];"
"function ac(){try{if(!AC)AC=new(window.AudioContext||window.webkitAudioContext);if(AC.state==='suspended')AC.resume()}catch(e){}return AC}"
"function beep(f,d,t,g){try{let a=ac();if(!a)return;let o=a.createOscillator(),v=a.createGain();o.type=t||'square';o.frequency.value=f;v.gain.value=g||.07;v.gain.exponentialRampToValueAtTime(.001,a.currentTime+d);o.connect(v);v.connect(a.destination);o.start();o.stop(a.currentTime+d)}catch(e){}}"
"function buzz(ms){try{navigator.vibrate&&navigator.vibrate(ms)}catch(e){}}"
"function fx(){if(!S)return;let me=S.p[slot];if(S.phase===1&&(!prev||prev.ph!==1))beep(880,.12,'sine',.1);"
"if(S.phase===2&&(!prev||prev.ph!==2)){beep(520,.18);buzz(30)}"
"if(S.pad&1&&(!prev||!(prev.pad&1))){beep(160,.45,'sawtooth',.16);buzz([90,40,90])}"
"if(me&&prev&&prev.hp!=null){if(me.hp<prev.hp){beep(140,.1,'sawtooth',.13);buzz(25)}if(!me.hp&&prev.hp){beep(80,.35);buzz(70)}"
"if(me.g>prev.g){beep(990,.16,'triangle',.11);buzz(12)}if(me.sh>prev.sh)beep(660,.08);if(me.weap!==prev.weap&&me.weap)beep(440,.07);"
"if(me.f&16&&!(prev.f&16))beep(520,.1);if(me.f&32&&!(prev.f&32)){beep(1200,.14,'square',.12);buzz(18)}}"
"if(prev&&S.pk.filter(u=>u.a).length<prev.pk)beep(720,.07,'sine',.09);"
"prev={ph:S.phase,pad:S.pad,hp:me?me.hp:0,g:me?me.g:0,sh:me?me.sh:0,weap:me?me.weap:0,f:me?me.f:0,pk:S.pk.filter(u=>u.a).length}}"
"onkeydown=e=>{K[e.code]=1;if(e.code==='Space')e.preventDefault()};"
"onkeyup=e=>K[e.code]=0;"
"let mx=0,my=0,ax=127,ay=0,fire=0;"
"C.onpointerdown=e=>{if(e.pointerType==='mouse')return;C.setPointerCapture(e.pointerId);P.set(e.pointerId,{sx:e.clientX,sy:e.clientY,r:e.clientX>innerWidth/2});"
"if(e.clientX>innerWidth/2)fire=1};"
"C.onpointermove=e=>{const p=P.get(e.pointerId);if(!p)return;let x=e.clientX-p.sx,y=e.clientY-p.sy,"
"m=Math.max(24,Math.hypot(x,y));x=Math.max(-1,Math.min(1,x/m));y=Math.max(-1,Math.min(1,y/m));"
"if(p.r){ax=x*127;ay=y*127}else{mx=x*127;my=y*127}};"
"C.onpointerup=C.onpointercancel=e=>{const p=P.get(e.pointerId);if(p){if(p.r){fire=0}else{mx=my=0}P.delete(e.pointerId)}};"
"C.onmousemove=e=>{if(P.size)return;let r=C.getBoundingClientRect(),x=e.clientX/r.width*960,y=e.clientY/r.height*600;"
"if(S&&slot<4&&S.p[slot]){ax=x-S.p[slot].x;ay=y-S.p[slot].y;let m=Math.max(1,Math.hypot(ax,ay));ax=ax/m*127;ay=ay/m*127}};"
"C.onmousedown=e=>{if(e.button===0)fire=1};C.onmouseup=e=>{if(e.button===0)fire=0};"
"ready.onclick=()=>{ready=1;J.style.display='none';ac();beep(660,.1)};"
"again.onclick=()=>{if(again.disabled)return;ready=1;GO.style.display='none';ac();beep(880,.12)};"
"function i8(v){return Math.max(-127,Math.min(127,v|0))}"
"let W=new WebSocket('ws://'+location.host+'/ws');W.binaryType='arraybuffer';"
"W.onopen=()=>H.textContent='connected · press READY';W.onclose=()=>H.textContent='disconnected';"
"W.onmessage=e=>parse(e.data);"
"setInterval(()=>{if(W.readyState!==1)return;let x=(K.KeyD?127:0)-(K.KeyA?127:0),y=(K.KeyS?127:0)-(K.KeyW?127:0);"
"if(x||y){mx=x;my=y}else if(![...P.values()].some(p=>!p.r)){mx=my=0}let qx=(K.ArrowRight?127:0)-(K.ArrowLeft?127:0),qy=(K.ArrowDown?127:0)-(K.ArrowUp?127:0);"
"if(qx||qy){ax=qx;ay=qy}let shot=fire?1:0;if(shot&&S&&S.phase===2){let t=performance.now();if(t-lf>90){lf=t;beep((S.p[slot]&&S.p[slot].weap===1)?1400:220,.04,'square',.05)}}"
"let b=new Int8Array([1,seq++&255,i8(mx),i8(my),i8(ax),i8(ay),shot,ready]);W.send(b)},33);"
"function u16(d,o){return d.getUint16(o,true)} function s16(d,o){return d.getInt16(o,true)}"
"function parse(b){let d=new DataView(b),o=0;if(d.getUint8(o++)!==129)return;slot=d.getUint8(o++);let phase=d.getUint8(o++),np=d.getUint8(o++),tick=u16(d,o);o+=2;"
"let pt=u16(d,o);o+=2,aw=u16(d,o);o+=2,ah=u16(d,o);o+=2,ne=d.getUint8(o++),ns=d.getUint8(o++),goal=d.getUint8(o++),pad=d.getUint8(o++);"
"let p=[];for(let i=0;i<4;i++){let z={a:d.getUint8(o),f:d.getUint8(o+1),x:u16(d,o+2),y:u16(d,o+4),ax:d.getInt8(o+6),ay:d.getInt8(o+7),hp:d.getUint8(o+8),score:s16(d,o+9),g:d.getUint8(o+11),k:d.getUint8(o+12),gx:u16(d,o+13),gy:u16(d,o+15),c:d.getUint8(o+17),sh:d.getUint8(o+18),weap:d.getUint8(o+19),gc:d.getUint8(o+20)};o+=21;p.push(z)}"
"let en=[];for(let i=0;i<8;i++){en.push({a:d.getUint8(o),hp:d.getUint8(o+1),x:u16(d,o+2),y:u16(d,o+4)});o+=8}"
"let sh=[];for(let i=0;i<64;i++){sh.push({a:d.getUint8(o),h:d.getUint8(o+1),x:u16(d,o+2),y:u16(d,o+4),own:d.getUint8(o+8),k:d.getUint8(o+9)});o+=10}"
"let w=[];for(let i=0;i<8;i++){w.push({x:u16(d,o),y:u16(d,o+2),w:u16(d,o+4),h:u16(d,o+6)});o+=8}"
"let pk=[];for(let i=0;i<6;i++){pk.push({a:d.getUint8(o),k:d.getUint8(o+1),x:u16(d,o+2),y:u16(d,o+4)});o+=6}"
"S={phase,np,tick,pt,aw,ah,ne,ns,goal,pad,p,en,sh,w,pk};"
"if(phase===3){if(!prev||prev.ph!==3){ready=0;again.disabled=true;if(goWait)clearTimeout(goWait);goWait=setTimeout(()=>{again.disabled=false},2000)}}"
"fx();"
"if(phase===3){let teams=pad&2;"
"if(teams){let t=[{n:'TEAM GY',c:0,score:0,g:0,a:0},{n:'TEAM BR',c:2,score:0,g:0,a:0}];"
"p.forEach((z,i)=>{if(!z.a)return;let T=t[i<2?0:1];T.a=1;T.score+=z.score;if(z.g>T.g)T.g=z.g});"
"let rows=t.filter(z=>z.a).sort((a,b)=>b.score-a.score||b.g-a.g);let mg=0;rows.forEach(z=>{if(z.g>mg)mg=z.g});"
"GL.innerHTML=rows.map((z,n)=>'<div style=color:'+col[z.c]+'>'+(n+1)+'. '+z.n+' · '+z.score+' pts · '+z.g+' gates'+(mg&&z.g===mg?' ★':'')+'</div>').join('')}"
"else{let rows=p.map((z,i)=>({i,a:z.a,score:z.score,g:z.g,bot:z.f&8,c:z.c})).filter(z=>z.a);"
"rows.sort((a,b)=>b.score-a.score||b.g-a.g);let mg=0;rows.forEach(z=>{if(z.g>mg)mg=z.g});"
"GL.innerHTML=rows.map((z,n)=>'<div style=color:'+col[z.c]+'>'+(n+1)+'. '+(z.bot?'BOT':'P'+(z.i+1))+' · '+z.score+' pts · '+z.g+' gates'+(mg&&z.g===mg?' ★':'')+'</div>').join('')}"
"GO.style.display='grid'}else GO.style.display='none';"
"if(phase===0&&!ready)J.style.display='grid';else J.style.display='none'}"
"function sm(a,b,n){if(a===undefined)return b;let d=b-a;if(d>n/2)d-=n;if(d<-n/2)d+=n;return(a+d*.45+n)%n}"
"function ship(x,y,dx,dy,c){let a=Math.atan2(dy,dx);X.save();X.translate(x,y);X.rotate(a);X.fillStyle=c;X.strokeStyle='#fff';X.lineWidth=1.5;X.beginPath();X.moveTo(16,0);X.lineTo(-11,10);X.lineTo(-6,0);X.lineTo(-11,-10);X.closePath();X.fill();X.stroke();X.restore()}"
"function draw(){requestAnimationFrame(draw);X.fillStyle='#060914';X.fillRect(0,0,960,600);X.strokeStyle='#142238';X.lineWidth=1;for(let x=0;x<960;x+=60){X.beginPath();X.moveTo(x,0);X.lineTo(x,600);X.stroke()}for(let y=0;y<600;y+=60){X.beginPath();X.moveTo(0,y);X.lineTo(960,y);X.stroke()}if(!S)return;"
"S.w.forEach(w=>{if(!w.w||!w.h)return;X.fillStyle='#1a2d52';X.fillRect(w.x,w.y,w.w,w.h);X.strokeStyle='#6aa0ff';X.lineWidth=2;X.strokeRect(w.x+.5,w.y+.5,w.w,w.h)});"
"let me=S.p[slot]||S.p[0],seen={};"
"S.p.forEach((p,i)=>{if(!p.a)return;let k=p.gx+','+p.gy;if(seen[k])return;seen[k]=1;"
"X.save();X.globalAlpha=i===slot||(S.pad&2&&((i^slot)&2)===0)?1:.4;X.strokeStyle=gcol[p.gc%6];X.lineWidth=i===slot?4:2;X.beginPath();X.arc(p.gx,p.gy,9,0,7);X.stroke();X.fillStyle=gcol[p.gc%6];X.fillText((S.pad&2?(i<2?'GY ':'BR '):(i===slot?'GATE ':'P'+(i+1)+' '))+(p.g+1)+'/'+S.goal,p.gx-28,p.gy-16);X.restore()});"
"S.pk.forEach(u=>{if(!u.a)return;X.fillStyle=pkcol[u.k%5];X.beginPath();X.arc(u.x,u.y,11,0,7);X.fill();X.fillStyle='#061018';X.font='11px system-ui';X.fillText(['+','S','L','*','G'][u.k%5],u.x-4,u.y+4)});"
"S.en.forEach((e,i)=>{if(!e.a){V.e[i]=null;return}let v=V.e[i]||(V.e[i]={});v.x=sm(v.x,e.x,960);v.y=sm(v.y,e.y,600);X.fillStyle='#d64cff';X.save();X.translate(v.x,v.y);X.rotate(Math.PI/4);X.fillRect(-10,-10,20,20);X.restore()});"
"S.sh.forEach((s,i)=>{if(!s.a){V.s[i]=null;return}let v=V.s[i]||(V.s[i]={});v.x=sm(v.x,s.x,960);v.y=sm(v.y,s.y,600);X.fillStyle=s.h?'#ff4ca0':(s.k===1?'#fff6a0':s.k===2?'#ffb04a':(s.own<4?col[s.own]:'#fff'));X.beginPath();X.arc(v.x,v.y,s.k===1?5:s.k===2?2.5:3.2,0,7);X.fill()});"
"S.p.forEach((p,i)=>{if(!p.a)return;let v=V.p[i]||(V.p[i]={});v.x=sm(v.x,p.x,960);v.y=sm(v.y,p.y,600);X.fillStyle=col[p.c];X.beginPath();X.arc(v.x,v.y,18,0,7);X.globalAlpha=.25;X.fill();X.globalAlpha=1;if(!p.hp){X.fillStyle='#888';X.fillText('respawn',v.x-20,v.y);return}if(p.sh||p.f&4){X.strokeStyle='#3ceaff';X.lineWidth=3;X.beginPath();X.arc(v.x,v.y,20,0,7);X.stroke()}if(p.f&16){X.strokeStyle='#ff6bff';X.lineWidth=2;X.beginPath();X.arc(v.x,v.y,23,0,7);X.stroke()}if(p.f&32){X.strokeStyle='#fff6a0';X.lineWidth=2;X.beginPath();X.arc(v.x,v.y,26,0,7);X.stroke()}ship(v.x,v.y,p.ax||1,p.ay,col[p.c]);X.fillStyle=col[p.c];X.font='bold 12px system-ui';X.fillText((p.f&8)?'BOT':'P'+(i+1),v.x-10,v.y-22);X.fillStyle='#fff';X.fillText(p.hp+(p.sh?'/'+p.sh:''),v.x-12,v.y+28)});"
"let ph=['LOBBY','COUNTDOWN','PLAY','RESULTS'][S.phase];H.style.color=col[(me&&me.c)||0];"
"let clk=S.phase===2?((S.pad&1)?'SUDDEN':Math.ceil(S.pt/30)+'s'):S.phase===1?Math.ceil(S.pt/30):'';"
"let nb=S.p.filter(z=>z.a&&(z.f&8)).length;"
"H.innerHTML=ph+' · <b>P'+(slot+1)+'</b> · '+(wn[(me&&me.weap)||0])+(clk?' · '+clk:'')+' · players '+S.np+(nb?' · bots '+nb:'')+(S.pad&2?' · 2v2':'')+'<br>gates '+(me?me.g:0)+'/'+S.goal+' · score '+(me?me.score:0)+' · kills '+(me?me.k:0)+(me&&me.sh?' · SHIELD '+me.sh:'')+(me&&(me.f&16)?' · GRIP':'')+(me&&(me.f&32)?' · STREAK':'');}"
"draw();</script></body></html>";

static void note(const char *s) {
    if (s_puts) s_puts(s);
}

static void ensure_pass(void) {
    if (!fwog_pass_ok(s_pass)) {
        fwog_pass_nvs_ensure(FWOG_PASS_KEY_ARENA, s_pass);
    }
}

static void send_game_on(void) {
    char line[48];
    snprintf(line, sizeof line, "BN GAME on ssid=%s pass=%s\n", BB_SSID, s_pass);
    note(line);
}

static void apply_ap_cfg(void) {
    wifi_config_t cfg = { 0 };
    memcpy(cfg.ap.ssid, BB_SSID, sizeof BB_SSID - 1u);
    cfg.ap.ssid_len = sizeof BB_SSID - 1u;
    cfg.ap.ssid_hidden = 0;
    memcpy(cfg.ap.password, s_pass, 8u);
    cfg.ap.channel = 6;
    cfg.ap.beacon_interval = 100;
    cfg.ap.dtim_period = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    cfg.ap.pmf_cfg.required = false;
    (void)esp_wifi_set_mode(WIFI_MODE_AP);
    (void)esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

static int slot_for_fd(int fd) {
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        if (s_fd[i] == fd) return (int)i;
    }
    return -1;
}

static int add_fd(int fd) {
    int slot = slot_for_fd(fd);
    if (slot >= 0) return slot;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    slot = bb_game_connect(&s_game);
    if (slot >= 0) s_fd[slot] = fd;
    xSemaphoreGive(s_mu);
    battlebridge_stat();
    return slot;
}

static void drop_fd(int fd) {
    xSemaphoreTake(s_mu, portMAX_DELAY);
    int slot = slot_for_fd(fd);
    if (slot >= 0) {
        s_fd[slot] = -1;
        s_snap_busy[slot] = 0;
        bb_game_disconnect(&s_game, (unsigned)slot);
    }
    xSemaphoreGive(s_mu);
    if (slot >= 0) battlebridge_stat();
}

static esp_err_t root_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, k_index, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cover_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
    const size_t n = (size_t)(cover_jpg_end - cover_jpg_start);
    return httpd_resp_send(req, (const char *)cover_jpg_start, n);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        return add_fd(fd) >= 0 ? ESP_OK : ESP_FAIL;
    }
    httpd_ws_frame_t f = { 0 };
    f.type = HTTPD_WS_TYPE_BINARY;
    esp_err_t e = httpd_ws_recv_frame(req, &f, 0);
    if (e != ESP_OK || f.len != 8u) return e;
    uint8_t b[8];
    f.payload = b;
    e = httpd_ws_recv_frame(req, &f, sizeof b);
    if (e != ESP_OK || b[0] != 1u) return e;
    int slot = slot_for_fd(fd);
    if (slot < 0) slot = add_fd(fd);
    if (slot < 0) return ESP_FAIL;
    bb_input_t in = {
        .move_x = (int8_t)b[2], .move_y = (int8_t)b[3],
        .aim_x = (int8_t)b[4], .aim_y = (int8_t)b[5],
        .fire = b[6], .ready = b[7],
    };
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bb_game_input(&s_game, (unsigned)slot, &in);
    xSemaphoreGive(s_mu);
    return ESP_OK;
}

static void ap_force_legacy(void) {
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    const uint8_t proto = (uint8_t)(WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (esp_wifi_set_protocol(WIFI_IF_AP, proto) != ESP_OK) {
        wifi_protocols_t p = { 0 };
        p.ghz_2g = proto;
        (void)esp_wifi_set_protocols(WIFI_IF_AP, &p);
    }
    (void)esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    (void)esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
}

static void snap_done(esp_err_t err, int fd, void *arg) {
    const unsigned i = (unsigned)(uintptr_t)arg;
    if (i < BB_PLAYERS) s_snap_busy[i] = 0;
    if (err != ESP_OK && s_httpd) {
        (void)httpd_sess_trigger_close(s_httpd, fd);
    }
}

static void close_fn(httpd_handle_t hd, int fd) {
    (void)hd;
    drop_fd(fd);
    close(fd);
}

static uint16_t px(int32_t q, uint16_t max) {
    int32_t v = q / 256;
    if (v < 0) v = 0;
    if (v >= max) v = max - 1u;
    return (uint16_t)v;
}

static int8_t vel8(int16_t q) {
    int v = q / 256;
    if (v < -127) v = -127;
    if (v > 127) v = 127;
    return (int8_t)v;
}

static void snapshot(net_snapshot_t *n, unsigned slot) {
    memset(n, 0, sizeof *n);
    n->h.type = 0x81;
    n->h.slot = (uint8_t)slot;
    n->h.phase = s_game.phase;
    n->h.players = (uint8_t)bb_game_active(&s_game);
    n->h.tick = s_game.tick;
    n->h.phase_ticks = s_game.phase_ticks;
    n->h.arena_w = BB_ARENA_W;
    n->h.arena_h = BB_ARENA_H;
    n->h.enemies = (uint8_t)bb_game_enemies(&s_game);
    n->h.shots = (uint8_t)bb_game_projectiles(&s_game);
    n->h.gate_goal = BB_GATE_GOAL;
    n->h.pad = (uint8_t)((s_game.sudden ? 1u : 0u) | (s_game.teams ? 2u : 0u));
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        const bb_player_t *p = &s_game.p[i];
        net_player_t *d = &n->p[i];
        d->active = p->active;
        d->flags = (uint8_t)((p->ready ? 1u : 0u) | (p->invul ? 2u : 0u) |
                             (p->shield ? 4u : 0u) | (p->bot ? 8u : 0u) |
                             (p->grip_ttl ? 16u : 0u) |
                             (p->streak >= 3u ? 32u : 0u));
        d->x = px(p->x, BB_ARENA_W);
        d->y = px(p->y, BB_ARENA_H);
        d->aim_x = p->aim_x;
        d->aim_y = p->aim_y;
        d->hp = p->hp;
        d->score = p->score;
        d->gates = p->gates;
        d->kills = p->kills;
        d->gate_x = p->gate_x;
        d->gate_y = p->gate_y;
        d->color = p->color;
        d->shield = p->shield;
        d->weap = p->weap;
        d->gate_c = p->gate_c;
    }
    for (unsigned i = 0; i < BB_ENEMIES; i++) {
        n->e[i].active = s_game.e[i].active;
        n->e[i].hp = s_game.e[i].hp;
        n->e[i].x = px(s_game.e[i].x, BB_ARENA_W);
        n->e[i].y = px(s_game.e[i].y, BB_ARENA_H);
        n->e[i].target = s_game.e[i].target;
    }
    for (unsigned i = 0; i < BB_PROJECTILES; i++) {
        n->shot[i].active = s_game.shot[i].active;
        n->shot[i].hostile = s_game.shot[i].hostile;
        n->shot[i].x = px(s_game.shot[i].x, BB_ARENA_W);
        n->shot[i].y = px(s_game.shot[i].y, BB_ARENA_H);
        n->shot[i].vx = vel8(s_game.shot[i].vx);
        n->shot[i].vy = vel8(s_game.shot[i].vy);
        n->shot[i].owner = s_game.shot[i].owner;
        n->shot[i].kind = s_game.shot[i].kind;
    }
    memcpy(n->wall, s_game.wall, sizeof n->wall);
    for (unsigned i = 0; i < BB_PICKUPS; i++) {
        n->pk[i].active = s_game.pk[i].active;
        n->pk[i].kind = s_game.pk[i].kind;
        n->pk[i].x = s_game.pk[i].x;
        n->pk[i].y = s_game.pk[i].y;
    }
}

static void game_task(void *arg) {
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BB_TICK_MS));
        if (!s_on || !s_httpd) continue;
        const int64_t step_start = esp_timer_get_time();
        xSemaphoreTake(s_mu, portMAX_DELAY);
        bb_game_step(&s_game);
        xSemaphoreGive(s_mu);
        const uint32_t step_us = (uint32_t)(esp_timer_get_time() - step_start);
        if (step_us > s_step_peak_us) s_step_peak_us = step_us;
        for (unsigned i = 0; i < BB_PLAYERS; i++) {
            const int fd = s_fd[i];
            if (fd < 0 ||
                httpd_ws_get_fd_info(s_httpd, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
                continue;
            }
            if (s_snap_busy[i]) continue;
            s_snap_busy[i] = 1;
            xSemaphoreTake(s_mu, portMAX_DELAY);
            snapshot(&s_snap[i], i);
            xSemaphoreGive(s_mu);
            httpd_ws_frame_t f = {
                .final = true, .type = HTTPD_WS_TYPE_BINARY,
                .payload = (uint8_t *)&s_snap[i], .len = sizeof s_snap[i],
            };
            if (httpd_ws_send_data_async(s_httpd, fd, &f, snap_done,
                                         (void *)(uintptr_t)i) != ESP_OK) {
                s_snap_busy[i] = 0;
                (void)httpd_sess_trigger_close(s_httpd, fd);
            }
        }
    }
}

static void http_start(void) {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 7;
    cfg.max_uri_handlers = 4;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 8;
    cfg.send_wait_timeout = 8;
    cfg.close_fn = close_fn;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        s_httpd = NULL;
        ESP_LOGE("fwog-arena", "httpd start failed");
        note("BN GAME fail=http\n");
        return;
    }
    ESP_LOGI("fwog-arena", "httpd :80");
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get,
    };
    const httpd_uri_t ws = {
        .uri = "/ws", .method = HTTP_GET, .handler = ws_handler,
        .is_websocket = true,
    };
    const httpd_uri_t cover = {
        .uri = "/cover.jpg", .method = HTTP_GET, .handler = cover_get,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &ws));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &cover));
}

void battlebridge_init(void (*puts_cb)(const char *)) {
    s_puts = puts_cb;
    s_mu = xSemaphoreCreateMutex();
    ensure_pass();
    bb_game_init(&s_game, esp_random());
    xTaskCreate(game_task, "arena_game", 8192, NULL, 5, &s_task);
}

void battlebridge_start(void) {
    if (s_on) {
        send_game_on();
        battlebridge_stat();
        return;
    }
    ensure_pass();
    memset(s_fd, 0xFF, sizeof s_fd);
    memset((void *)s_snap_busy, 0, sizeof s_snap_busy);
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bb_game_init(&s_game, esp_random());
    bb_game_set_bots(&s_game, s_bots);
    bb_game_set_teams(&s_game, s_teams);
    xSemaphoreGive(s_mu);
    (void)esp_wifi_stop();
    apply_ap_cfg();
    ap_force_legacy();
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK) {
        char line[64];
        snprintf(line, sizeof line, "BN GAME fail=%s\n", esp_err_to_name(e));
        note(line);
        note("BN GAME off\n");
        return;
    }
    ap_force_legacy();
    http_start();
    s_on = s_httpd != NULL;
    if (!s_on) {
        (void)esp_wifi_stop();
        note("BN GAME off\n");
        return;
    }
    send_game_on();
    battlebridge_stat();
}

void battlebridge_stop(void) {
    s_on = false;
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    (void)esp_wifi_stop();
    for (unsigned i = 0; i < BB_PLAYERS; i++) {
        s_fd[i] = -1;
        s_snap_busy[i] = 0;
    }
    note("BN GAME off\n");
}

void battlebridge_go(void) {
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bb_game_force_start(&s_game);
    xSemaphoreGive(s_mu);
    battlebridge_stat();
}

void battlebridge_set_bots(unsigned skill) {
    s_bots = skill;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bb_game_set_bots(&s_game, skill);
    xSemaphoreGive(s_mu);
    battlebridge_stat();
}

void battlebridge_set_teams(bool on) {
    s_teams = on;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bb_game_set_teams(&s_game, on);
    xSemaphoreGive(s_mu);
    battlebridge_stat();
}

void battlebridge_wipe(void) {
    fwog_pass_nvs_rotate(FWOG_PASS_KEY_ARENA, s_pass);
    if (s_on) {
        apply_ap_cfg();
        (void)esp_wifi_deauth_sta(0);
        send_game_on();
        battlebridge_stat();
    }
    char line[40];
    snprintf(line, sizeof line, "BN GAME wipe pass=%s\n", s_pass);
    note(line);
}

bool battlebridge_is_on(void) { return s_on; }

void battlebridge_stat(void) {
    char line[200];
    xSemaphoreTake(s_mu, portMAX_DELAY);
    const unsigned players = bb_game_active(&s_game);
    const unsigned phase = s_game.phase;
    const unsigned tick = s_game.tick;
    const unsigned bots = s_game.bots_on;
    const unsigned sudden = s_game.sudden;
    const unsigned teams = s_game.teams;
    unsigned clock_s = 0;
    if (phase == BB_PHASE_PLAY || phase == BB_PHASE_COUNTDOWN) {
        clock_s = ((unsigned)s_game.phase_ticks + 29u) / 30u;
        if (clock_s > 255u) clock_s = 255u;
    }
    xSemaphoreGive(s_mu);
    const unsigned step_us = s_step_peak_us;
    s_step_peak_us = 0;
    if (!s_on) {
        note("BN GAME off\n");
        return;
    }
    snprintf(line, sizeof line,
             "BN GAME sta players=%u phase=%u tick=%u heap=%u minheap=%u "
             "step=%u bots=%u clock=%u sudden=%u teams=%u\n",
             players, phase, tick,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             step_us, bots, clock_s, sudden, teams);
    note(line);
}
