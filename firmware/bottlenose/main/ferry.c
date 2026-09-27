/* LanFerry SoftAP: 124 KiB mailbox, message board, multicast pipe. */
#include "ferry.h"
#include "pass_nvs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_coexist.h"

static const char *TAG = "fwog-ferry";

#define FILES_MAX      24
#define FERRY_NAME_MAX 32
#define PIPE_N         8192
#define PIPE_RX_MAX    4
#define BOARD_N        16
#define BOARD_MSG      160
#define PIPE_STAT_MS   250u

typedef struct {
    char     name[FERRY_NAME_MAX];
    uint32_t off;
    uint32_t len;
} ferry_file_t;

typedef struct {
    bool     on;
    size_t   r;
    uint32_t gen;
} pipe_rx_t;

static void (*s_puts)(const char *);
static esp_netif_t *s_ap_netif;
static char     s_pass[9];
static uint8_t  s_pool[FERRY_POOL_MAX];
static size_t   s_used;
static ferry_file_t s_files[FILES_MAX];
static unsigned s_nfiles;
static uint8_t  s_clients;
static bool     s_ap_on;
static httpd_handle_t s_httpd;

static uint8_t  s_ring[PIPE_N];
static size_t   s_w;
static pipe_rx_t s_rx[PIPE_RX_MAX];
static unsigned s_nrx;
static bool     s_pipe_put, s_pipe_done;
static uint32_t s_pipe_gen;
static char     s_pipe_name[FERRY_NAME_MAX];
static uint32_t s_pipe_sent, s_pipe_total;
static TickType_t s_pipe_stat_at;
static SemaphoreHandle_t s_pipe_mu;
static SemaphoreHandle_t s_pipe_ev;
static QueueHandle_t s_pipe_jobs;

static char     s_board[BOARD_N][BOARD_MSG];
static uint8_t  s_board_head, s_board_n;

static const char k_index[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>FWOG-ferry</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:40em;margin:1em}"
    "#board{height:10em;overflow:auto;border:1px solid #888;padding:.5em;"
    "white-space:pre-wrap;background:#111;color:#ddd}"
    "a{color:#8cf} .err{color:#c33} .ok{color:#6a6}"
    "</style></head><body>"
    "<h1>FWOG-ferry</h1>"
    "<p>Drop: <b>http://192.168.4.1/</b></p>"
    "<p>Mailbox <span id=used>0</span> / <span id=pool>?</span> bytes. "
    "Gray hold on the OG wipes RAM and this board.</p>"
    "<h2>Mailbox</h2>"
    "<input id=f type=file multiple><button onclick=store()>store</button>"
    "<div id=list></div><pre id=s></pre>"
    "<h2>Board</h2>"
    "<div id=board></div>"
    "<textarea id=t rows=3 style='width:100%' placeholder='paste a note'></textarea><br>"
    "<button onclick=paste()>post</button>"
    "<h2>Live pipe</h2>"
    "<p>Every peer clicks <b>receive</b> first. Sender then picks a file and "
    "<b>send to all</b>. The pipe is a live stream — the file can be larger "
    "than the 124K mailbox and is not stored.</p>"
    "<p id=pipe>idle</p>"
    "<button onclick=recv()>receive</button> "
    "<input id=sf type=file><button onclick=sendLive()>send to all</button>"
    "<h2>Peers</h2><pre id=p></pre>"
    "<script>"
    "let busy=0, rxid=-1;"
    "function sleep(ms){return new Promise(r=>setTimeout(r,ms));}"
    "function set(id,t,cls){const e=document.getElementById(id);if(!e)return;"
    "e.textContent=t;e.className=cls||'';}"
    "async function refresh(){"
    " if(busy) return 0;"
    " try{"
    "  const j=await (await fetch('/status')).json();"
    "  document.getElementById('used').textContent=j.used;"
    "  document.getElementById('pool').textContent=j.pool;"
    "  const room=j.pool-j.used;"
    "  document.getElementById('list').innerHTML=(j.files&&j.files.length)?"
    "   j.files.map(f=>'<div><a href=\"/files/'+encodeURIComponent(f.name)+"
    "   '\" download>'+f.name+'</a>  '+f.bytes+' bytes</div>').join('') :"
    "   '<i>no files</i>';"
    "  document.getElementById('board').textContent=(j.msgs&&j.msgs.join('\\n'))||'(empty)';"
    "  const bd=document.getElementById('board'); bd.scrollTop=bd.scrollHeight;"
    "  document.getElementById('p').textContent=(j.peers&&j.peers.map(x=>x.ip+'  '+x.mac).join('\\n'))||'(none)';"
    "  const p=j.pipe||{};"
    "  if(!busy){"
    "   if(p.put && p.total) set('pipe','sending '+(p.name||'')+'  '+Math.floor(100*p.bytes/p.total)+'%  to '+p.recv+' peer(s)');"
    "   else if(p.put) set('pipe','sending '+(p.name||'')+'  '+p.bytes+' bytes  to '+p.recv+' peer(s)');"
    "   else if(p.recv) set('pipe',p.recv+' receiver(s) waiting');"
    "   else set('pipe','idle');"
    "  }"
    "  return room;"
    " }catch(e){return 0;}"
    "}"
    "async function store(){"
    " const room=await refresh();"
    " const fs=document.getElementById('f').files;"
    " for(const f of fs){"
    "  if(f.size>room){set('s','too big: '+f.name+' is '+f.size+' bytes, '"
    "   +room+' free','err'); return;}"
    "  if(!f.size){set('s','empty file','err'); return;}"
    "  const r=await fetch('/put/'+encodeURIComponent(f.name),{method:'POST',body:f});"
    "  set('s',await r.text(), r.ok?'ok':'err');"
    "  if(!r.ok) return;"
    " }"
    " refresh();"
    "}"
    "async function paste(){"
    " const t=document.getElementById('t').value;"
    " if(!t)return;"
    " const r=await fetch('/board',{method:'POST',body:t});"
    " set('s', r.ok?'posted':'board failed', r.ok?'ok':'err');"
    " if(r.ok) document.getElementById('t').value='';"
    " refresh();"
    "}"
    "function b64d(s){const bin=atob(s);const u=new Uint8Array(bin.length);"
    " for(let i=0;i<bin.length;i++)u[i]=bin.charCodeAt(i);return u;}"
    "async function recv(){"
    " busy=1;"
    " rxid=-1;"
    " try{"
    "  for(;;){"
    "   set('pipe','waiting for sender — keep this tab open');"
    "   const a=await fetch('/pipe/arm',{method:'POST'});"
    "   if(!a.ok) throw new Error(await a.text());"
    "   rxid=(await a.json()).id;"
    "   const parts=[];"
    "   let name='ferry.bin';"
    "   for(;;){"
    "    const r=await fetch('/pipe/c/'+rxid,{signal:AbortSignal.timeout(15000)});"
    "    if(!r.ok) throw new Error(await r.text());"
    "    const j=await r.json();"
    "    if(j.t=='w'){await sleep(200);continue;}"
    "    if(j.n) name=j.n;"
    "    if(j.b){parts.push(b64d(j.b));"
    "     let n=0; for(const p of parts) n+=p.length;"
    "     set('pipe','receiving '+name+'  '+n+' bytes');}"
    "    if(j.t=='d'||j.d) break;"
    "   }"
    "   if(rxid>=0) fetch('/pipe/x/'+rxid,{method:'POST',keepalive:true});"
    "   rxid=-1;"
    "   if(!parts.length) continue;"
    "   const blob=new Blob(parts);"
    "   const x=document.createElement('a');"
    "   x.href=URL.createObjectURL(blob); x.download=name;"
    "   document.body.appendChild(x); x.click(); x.remove();"
    "   set('pipe','got '+name+' ('+blob.size+' bytes) — still receiving');"
    "  }"
    " }catch(e){set('pipe','receive failed: '+e,'err');}"
    " if(rxid>=0) fetch('/pipe/x/'+rxid,{method:'POST',keepalive:true});"
    " rxid=-1; busy=0;"
    "}"
    "addEventListener('pagehide',()=>{if(rxid>=0) fetch('/pipe/x/'+rxid,{method:'POST',keepalive:true});});"
    "async function sendLive(){"
    " const f=document.getElementById('sf').files[0];"
    " if(!f){set('pipe','pick a file first','err'); return;}"
    " busy=1;"
    " try{"
    "  const j=await (await fetch('/status')).json();"
    "  const n=(j.pipe&&j.pipe.recv)||0;"
    "  if(!n){set('pipe','no receivers — they click receive first','err'); busy=0; return;}"
    "  set('pipe','sending '+f.name+' to '+n+' peer(s)…');"
    "  const r=await fetch('/stream/'+encodeURIComponent(f.name),"
    "   {method:'POST',body:f,signal:AbortSignal.timeout(120000)});"
    "  set('pipe',await r.text(), r.ok?'ok':'err');"
    " }catch(e){set('pipe','send failed: '+e,'err');}"
    " busy=0;"
    "}"
    "refresh(); setInterval(refresh,500);"
    "</script></body></html>";

static void bn_puts(const char *s) {
    if (s_puts) s_puts(s);
}

static void ensure_pass(void) {
    if (!fwog_pass_ok(s_pass)) {
        fwog_pass_nvs_ensure(FWOG_PASS_KEY_FERRY, s_pass);
    }
}

static void sanitize_name(char *s) {
    for (char *p = s; *p; p++) {
        if (*p < 32 || *p > 126 || *p == ' ' || *p == '/' || *p == '\\' ||
            *p == '<' || *p == '>' || *p == '"') *p = '_';
    }
}

static int hexn(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode(char *s) {
    char *d = s;
    while (*s) {
        if (*s == '%' && hexn(s[1]) >= 0 && hexn(s[2]) >= 0) {
            *d++ = (char)((hexn(s[1]) << 4) | hexn(s[2]));
            s += 3;
        } else if (*s == '+') {
            *d++ = ' ';
            s++;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

static int file_find(const char *name) {
    for (unsigned i = 0; i < s_nfiles; i++) {
        if (strcmp(s_files[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void file_remove_i(unsigned i) {
    if (i >= s_nfiles) return;
    const uint32_t off = s_files[i].off;
    const uint32_t len = s_files[i].len;
    if (len) {
        memmove(s_pool + off, s_pool + off + len, s_used - (off + len));
    }
    for (unsigned j = i + 1u; j < s_nfiles; j++) {
        if (s_files[j].off > off) s_files[j].off -= len;
        s_files[j - 1u] = s_files[j];
    }
    s_nfiles--;
    s_used -= len;
    memset(&s_files[s_nfiles], 0, sizeof s_files[0]);
}

static void board_clear(void) {
    s_board_n = 0;
    s_board_head = 0;
    memset(s_board, 0, sizeof s_board);
}

static void board_add(const char *s) {
    char *dst = s_board[(s_board_head + s_board_n) % BOARD_N];
    if (s_board_n == BOARD_N) {
        s_board_head = (uint8_t)((s_board_head + 1u) % BOARD_N);
        dst = s_board[(s_board_head + BOARD_N - 1u) % BOARD_N];
    } else {
        s_board_n++;
    }
    memset(dst, 0, BOARD_MSG);
    strncpy(dst, s, BOARD_MSG - 1u);
    for (char *p = dst; *p; p++) {
        if (*p < 32 && *p != '\n') *p = ' ';
    }
}

static void pool_clear(void) {
    s_nfiles = 0;
    s_used = 0;
    memset(s_files, 0, sizeof s_files);
    board_clear();
}

static const char *last_name(void) {
    return s_nfiles ? s_files[s_nfiles - 1u].name : "-";
}

size_t ferry_pool_max(void) { return FERRY_POOL_MAX; }

static void send_ap_on(void) {
    char line[48];
    snprintf(line, sizeof line, "BN AP on pass=%s\n", s_pass);
    bn_puts(line);
}

static void send_sta(void) {
    char line[160];
    snprintf(line, sizeof line,
             "BN STA clients=%u files=%u used=%u last=%s\n",
             (unsigned)s_clients, s_nfiles, (unsigned)s_used, last_name());
    bn_puts(line);
}

static void send_pipe(void) {
    unsigned pct = 0;
    if (s_pipe_total) {
        pct = (unsigned)(((uint64_t)s_pipe_sent * 100u) / s_pipe_total);
        if (pct > 100u) pct = 100u;
    } else if (s_pipe_done && s_pipe_sent) {
        pct = 100u;
    }
    if (pct == 0u && s_pipe_put) pct = 1u;
    char line[32];
    snprintf(line, sizeof line, "BN PIPE pct=%u\n", pct);
    bn_puts(line);
}

static void pipe_stat_maybe(void) {
    TickType_t now = xTaskGetTickCount();
    if ((now - s_pipe_stat_at) < pdMS_TO_TICKS(PIPE_STAT_MS)) return;
    s_pipe_stat_at = now;
    send_pipe();
}

void ferry_stat(void) {
    if (s_ap_on) {
        send_sta();
        if (s_pipe_put) send_pipe();
    } else {
        bn_puts("BN AP off\n");
    }
}

void ferry_hello(void) {
}

bool ferry_ap_is_on(void) { return s_ap_on; }

static int pipe_wait(TickType_t ticks) {
    return xSemaphoreTake(s_pipe_ev, ticks) == pdTRUE ? 0 : -1;
}

static void pipe_kick(void) {
    xSemaphoreGive(s_pipe_ev);
}

static void pipe_abort(void) {
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    s_pipe_put = false;
    s_pipe_done = true;
    s_pipe_name[0] = '\0';
    s_pipe_sent = 0;
    s_pipe_total = 0;
    for (unsigned i = 0; i < PIPE_RX_MAX; i++) s_rx[i].on = false;
    s_nrx = 0;
    xSemaphoreGive(s_pipe_mu);
    pipe_kick();
    bn_puts("BN PIPE pct=0\n");
}

static size_t pipe_room(void) {
    if (!s_nrx) return 0;
    size_t min_free = PIPE_N - 1u;
    for (unsigned i = 0; i < PIPE_RX_MAX; i++) {
        if (!s_rx[i].on) continue;
        size_t used = (s_w + PIPE_N - s_rx[i].r) % PIPE_N;
        size_t freeb = PIPE_N - 1u - used;
        if (freeb < min_free) min_free = freeb;
    }
    return min_free;
}

static esp_err_t idx_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, k_index, HTTPD_RESP_USE_STRLEN);
}

static void name_from_uri(const char *uri, const char *pfx, char *dst, size_t n) {
    const char *p = uri;
    size_t pl = strlen(pfx);
    if (strncmp(uri, pfx, pl) == 0) p = uri + pl;
    else {
        const char *slash = strrchr(uri, '/');
        p = slash ? slash + 1 : uri;
    }
    strncpy(dst, p, n - 1u);
    dst[n - 1u] = '\0';
    url_decode(dst);
    sanitize_name(dst);
    if (!dst[0]) strncpy(dst, "file.bin", n);
}

static void json_esc(char *buf, size_t *n, size_t cap, const char *s) {
    for (; *s && *n + 2u < cap; s++) {
        if (*s == '"' || *s == '\\') buf[(*n)++] = '\\';
        if ((unsigned char)*s >= 32) buf[(*n)++] = *s;
        else if (*s == '\n' && *n + 2u < cap) {
            buf[(*n)++] = '\\';
            buf[(*n)++] = 'n';
        }
    }
}

static void peers_json_inner(char *buf, size_t *n, size_t cap) {
    wifi_sta_list_t list;
    wifi_sta_mac_ip_list_t iplist;
    memset(&list, 0, sizeof list);
    memset(&iplist, 0, sizeof iplist);
    (void)esp_wifi_ap_get_sta_list(&list);
    (void)esp_wifi_ap_get_sta_list_with_ip(&list, &iplist);
    s_clients = (uint8_t)list.num;
    *n += (size_t)snprintf(buf + *n, cap - *n, "\"clients\":%d,\"peers\":[", list.num);
    for (int i = 0; i < iplist.num && *n + 64u < cap; i++) {
        *n += (size_t)snprintf(buf + *n, cap - *n,
                               "%s{\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"ip\":\"" IPSTR "\"}",
                               i ? "," : "",
                               iplist.sta[i].mac[0], iplist.sta[i].mac[1], iplist.sta[i].mac[2],
                               iplist.sta[i].mac[3], iplist.sta[i].mac[4], iplist.sta[i].mac[5],
                               IP2STR(&iplist.sta[i].ip));
    }
    *n += (size_t)snprintf(buf + *n, cap - *n, "]");
}

static esp_err_t status_get(httpd_req_t *req) {
    char buf[3072];
    size_t n = 0;
    n += (size_t)snprintf(buf + n, sizeof buf - n,
                          "{\"pool\":%u,\"used\":%u,\"files\":[",
                          (unsigned)FERRY_POOL_MAX, (unsigned)s_used);
    for (unsigned i = 0; i < s_nfiles && n + 80u < sizeof buf; i++) {
        n += (size_t)snprintf(buf + n, sizeof buf - n,
                              "%s{\"name\":\"", i ? "," : "");
        json_esc(buf, &n, sizeof buf, s_files[i].name);
        n += (size_t)snprintf(buf + n, sizeof buf - n, "\",\"bytes\":%u}",
                              (unsigned)s_files[i].len);
    }
    n += (size_t)snprintf(buf + n, sizeof buf - n, "],\"msgs\":[");
    for (unsigned i = 0; i < s_board_n && n + BOARD_MSG + 8u < sizeof buf; i++) {
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s\"", i ? "," : "");
        json_esc(buf, &n, sizeof buf, s_board[(s_board_head + i) % BOARD_N]);
        n += (size_t)snprintf(buf + n, sizeof buf - n, "\"");
    }
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    unsigned nrx = s_nrx;
    bool put = s_pipe_put;
    uint32_t sent = s_pipe_sent, total = s_pipe_total;
    char pname[FERRY_NAME_MAX];
    memcpy(pname, s_pipe_name, sizeof pname);
    xSemaphoreGive(s_pipe_mu);
    n += (size_t)snprintf(buf + n, sizeof buf - n,
                          "],\"pipe\":{\"recv\":%u,\"put\":%u,\"bytes\":%u,\"total\":%u,\"name\":\"",
                          nrx, put ? 1u : 0u, (unsigned)sent, (unsigned)total);
    json_esc(buf, &n, sizeof buf, pname);
    n += (size_t)snprintf(buf + n, sizeof buf - n, "\"},");
    peers_json_inner(buf, &n, sizeof buf);
    n += (size_t)snprintf(buf + n, sizeof buf - n, "}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, (ssize_t)n);
}

static esp_err_t files_get(httpd_req_t *req) {
    if (strcmp(req->uri, "/files") == 0 || strcmp(req->uri, "/files/") == 0) {
        return status_get(req);
    }
    char name[FERRY_NAME_MAX];
    name_from_uri(req->uri, "/files/", name, sizeof name);
    int i = file_find(name);
    if (i < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no file");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    char cd[80];
    snprintf(cd, sizeof cd, "attachment; filename=\"%s\"", s_files[i].name);
    httpd_resp_set_hdr(req, "Content-Disposition", cd);
    return httpd_resp_send(req, (const char *)(s_pool + s_files[i].off),
                           (ssize_t)s_files[i].len);
}

static esp_err_t file_get(httpd_req_t *req) {
    if (!s_nfiles) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no file");
        return ESP_OK;
    }
    const unsigned i = s_nfiles - 1u;
    httpd_resp_set_type(req, "application/octet-stream");
    char cd[80];
    snprintf(cd, sizeof cd, "attachment; filename=\"%s\"", s_files[i].name);
    httpd_resp_set_hdr(req, "Content-Disposition", cd);
    return httpd_resp_send(req, (const char *)(s_pool + s_files[i].off),
                           (ssize_t)s_files[i].len);
}

static esp_err_t put_handler(httpd_req_t *req) {
    char name[FERRY_NAME_MAX];
    name_from_uri(req->uri, "/put/", name, sizeof name);
    int exist = file_find(name);
    size_t reclaim = (exist >= 0) ? s_files[exist].len : 0;
    if (s_nfiles >= FILES_MAX && exist < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too many files");
        return ESP_OK;
    }
    const int clen = req->content_len;
    size_t room = FERRY_POOL_MAX - s_used + reclaim;
    if (clen == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_OK;
    }
    if (clen > 0 && (size_t)clen > room) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "too big for mailbox");
        return ESP_OK;
    }
    if (exist >= 0) file_remove_i((unsigned)exist);
    room = FERRY_POOL_MAX - s_used;
    size_t got = 0;
    const size_t cap = (clen > 0) ? (size_t)clen : room;
    while (got < cap) {
        int n = httpd_req_recv(req, (char *)s_pool + s_used + got, cap - got);
        if (n == 0) break;
        if (n < 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_OK;
        }
        got += (size_t)n;
    }
    if (clen < 0 && got == room) {
        char extra;
        if (httpd_req_recv(req, &extra, 1) > 0) {
            httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "too big for mailbox");
            return ESP_OK;
        }
    }
    if (got == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_OK;
    }
    strncpy(s_files[s_nfiles].name, name, FERRY_NAME_MAX - 1u);
    s_files[s_nfiles].off = (uint32_t)s_used;
    s_files[s_nfiles].len = (uint32_t)got;
    s_nfiles++;
    s_used += got;
    send_sta();
    char msg[96];
    snprintf(msg, sizeof msg, "stored %s (%u bytes)  %u files  %u / %u",
             name, (unsigned)got, s_nfiles, (unsigned)s_used,
             (unsigned)FERRY_POOL_MAX);
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t peers_get(httpd_req_t *req) {
    return status_get(req);
}

static esp_err_t board_get(httpd_req_t *req) {
    return status_get(req);
}

static esp_err_t board_post(httpd_req_t *req) {
    char msg[BOARD_MSG];
    memset(msg, 0, sizeof msg);
    int got = 0;
    const int clen = req->content_len;
    const int cap = (int)sizeof msg - 1;
    const int want = (clen > 0 && clen < cap) ? clen : cap;
    while (got < want) {
        int n = httpd_req_recv(req, msg + got, (size_t)(want - got));
        if (n <= 0) break;
        got += n;
    }
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_OK;
    }
    msg[got] = '\0';
    board_add(msg);
    return httpd_resp_send(req, "posted", HTTPD_RESP_USE_STRLEN);
}

static int rx_register(void) {
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    int slot = -1;
    for (unsigned i = 0; i < PIPE_RX_MAX; i++) {
        if (!s_rx[i].on) {
            slot = (int)i;
            s_rx[i].on = true;
            s_rx[i].r = s_w;
            s_rx[i].gen = s_pipe_put ? s_pipe_gen : 0;
            s_nrx++;
            break;
        }
    }
    xSemaphoreGive(s_pipe_mu);
    pipe_kick();
    return slot;
}

static void rx_unregister(int slot) {
    if (slot < 0) return;
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    if (s_rx[slot].on) {
        s_rx[slot].on = false;
        if (s_nrx) s_nrx--;
    }
    xSemaphoreGive(s_pipe_mu);
    pipe_kick();
}

static int id_from_uri(httpd_req_t *req, const char *pfx) {
    const char *u = req->uri;
    size_t n = strlen(pfx);
    if (strncmp(u, pfx, n) != 0) return -1;
    if (u[n] < '0' || u[n] > '9') return -1;
    int id = atoi(u + n);
    if (id < 0 || id >= (int)PIPE_RX_MAX) return -1;
    return id;
}

static size_t b64_enc(const uint8_t *in, size_t n, char *out, size_t cap) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned a = in[i];
        unsigned b = (i + 1 < n) ? in[i + 1] : 0;
        unsigned c = (i + 2 < n) ? in[i + 2] : 0;
        if (o + 5u > cap) break;
        out[o++] = T[a >> 2];
        out[o++] = T[((a & 3) << 4) | (b >> 4)];
        out[o++] = (i + 1 < n) ? T[((b & 15) << 2) | (c >> 6)] : '=';
        out[o++] = (i + 2 < n) ? T[c & 63] : '=';
    }
    if (o < cap) out[o] = '\0';
    return o;
}

static esp_err_t pipe_arm(httpd_req_t *req) {
    int slot = rx_register();
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pipe full");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "pipe arm id=%d nrx=%u", slot, s_nrx);
    send_pipe();
    char buf[24];
    snprintf(buf, sizeof buf, "{\"id\":%d}", slot);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t pipe_cancel(httpd_req_t *req) {
    int id = id_from_uri(req, "/pipe/x/");
    if (id >= 0) rx_unregister(id);
    ESP_LOGI(TAG, "pipe cancel id=%d nrx=%u", id, s_nrx);
    send_pipe();
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t pipe_chunk(httpd_req_t *req) {
    int id = id_from_uri(req, "/pipe/c/");
    if (id < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_OK;
    }
    uint8_t tmp[768];
    size_t take = 0;
    bool done = false;
    char name[FERRY_NAME_MAX];
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    if (!s_rx[id].on) {
        xSemaphoreGive(s_pipe_mu);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad id");
        return ESP_OK;
    }
    if (!s_pipe_put && s_rx[id].gen != s_pipe_gen) {
        xSemaphoreGive(s_pipe_mu);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, "{\"t\":\"w\"}", HTTPD_RESP_USE_STRLEN);
    }
    if (s_pipe_put && s_rx[id].gen != s_pipe_gen) {
        s_rx[id].gen = s_pipe_gen;
        s_rx[id].r = 0;
    }
    size_t r = s_rx[id].r;
    size_t n = (s_w + PIPE_N - r) % PIPE_N;
    take = n > sizeof tmp ? sizeof tmp : n;
    if (take) {
        if (r + take <= PIPE_N) {
            memcpy(tmp, s_ring + r, take);
        } else {
            size_t a = PIPE_N - r;
            memcpy(tmp, s_ring + r, a);
            memcpy(tmp + a, s_ring, take - a);
        }
        s_rx[id].r = (r + take) % PIPE_N;
        n -= take;
    }
    done = s_pipe_done && (n == 0) && (s_rx[id].gen == s_pipe_gen) && s_pipe_gen;
    snprintf(name, sizeof name, "%s",
             s_pipe_name[0] ? s_pipe_name : "ferry.bin");
    xSemaphoreGive(s_pipe_mu);
    pipe_kick();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (take == 0 && !done) {
        return httpd_resp_send(req, "{\"t\":\"w\"}", HTTPD_RESP_USE_STRLEN);
    }
    if (take == 0) {
        char buf[80];
        snprintf(buf, sizeof buf, "{\"t\":\"d\",\"n\":\"%s\"}", name);
        return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    }
    char json[1200];
    size_t o = (size_t)snprintf(json, sizeof json,
                                "{\"t\":\"b\",\"d\":%u,\"n\":\"%s\",\"b\":\"",
                                done ? 1u : 0u, name);
    o += b64_enc(tmp, take, json + o, sizeof json - o);
    if (o + 3u < sizeof json) {
        json[o++] = '"';
        json[o++] = '}';
        json[o] = '\0';
    }
    return httpd_resp_send(req, json, (ssize_t)o);
}

static void stream_put_run(httpd_req_t *req) {
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    if (s_pipe_put) {
        xSemaphoreGive(s_pipe_mu);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pipe busy");
        return;
    }
    name_from_uri(req->uri, "/stream/", s_pipe_name, sizeof s_pipe_name);
    if (strcmp(s_pipe_name, "stream") == 0) {
        strncpy(s_pipe_name, "ferry.bin", sizeof s_pipe_name);
    }
    s_pipe_put = true;
    s_pipe_done = false;
    s_pipe_gen++;
    if (!s_pipe_gen) s_pipe_gen = 1;
    s_pipe_sent = 0;
    s_pipe_total = (req->content_len > 0) ? (uint32_t)req->content_len : 0;
    s_w = 0;
    for (unsigned i = 0; i < PIPE_RX_MAX; i++) {
        if (s_rx[i].on) {
            s_rx[i].r = 0;
            s_rx[i].gen = s_pipe_gen;
        }
    }
    xSemaphoreGive(s_pipe_mu);
    ESP_LOGI(TAG, "pipe put %s nrx=%u total=%u", s_pipe_name, s_nrx,
             (unsigned)s_pipe_total);
    pipe_kick();
    send_pipe();
    TickType_t start = xTaskGetTickCount();
    TickType_t stuck_at = start;
    while (s_nrx == 0 && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(15000)) {
        pipe_wait(pdMS_TO_TICKS(200));
    }
    if (s_nrx == 0) {
        pipe_abort();
        httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "no receiver");
        return;
    }
    uint8_t chunk[1024];
    for (;;) {
        int n = httpd_req_recv(req, (char *)chunk, sizeof chunk);
        if (n == 0) break;
        if (n < 0) break;
        size_t off = 0;
        while (off < (size_t)n) {
            xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
            size_t room = pipe_room();
            size_t take = room < ((size_t)n - off) ? room : ((size_t)n - off);
            if (take) {
                size_t w = s_w;
                if (w + take <= PIPE_N) {
                    memcpy(s_ring + w, chunk + off, take);
                } else {
                    size_t a = PIPE_N - w;
                    memcpy(s_ring + w, chunk + off, a);
                    memcpy(s_ring, chunk + off + a, take - a);
                }
                s_w = (w + take) % PIPE_N;
                s_pipe_sent += (uint32_t)take;
                off += take;
            }
            unsigned nrx = s_nrx;
            bool putting = s_pipe_put;
            xSemaphoreGive(s_pipe_mu);
            if (take) {
                pipe_kick();
                stuck_at = xTaskGetTickCount();
            }
            pipe_stat_maybe();
            if (!nrx || !putting) {
                pipe_abort();
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "receivers gone");
                return;
            }
            if (!take && (xTaskGetTickCount() - stuck_at) > pdMS_TO_TICKS(8000)) {
                ESP_LOGW(TAG, "pipe stall, abort");
                pipe_abort();
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "receiver left");
                return;
            }
            if (off < (size_t)n) pipe_wait(pdMS_TO_TICKS(50));
        }
    }
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    s_pipe_done = true;
    xSemaphoreGive(s_pipe_mu);
    pipe_kick();
    send_pipe();
    start = xTaskGetTickCount();
    while (s_nrx && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(20000)) {
        pipe_wait(pdMS_TO_TICKS(100));
    }
    xSemaphoreTake(s_pipe_mu, portMAX_DELAY);
    s_pipe_put = false;
    s_pipe_done = false;
    uint32_t sent = s_pipe_sent;
    unsigned nrx = s_nrx;
    xSemaphoreGive(s_pipe_mu);
    char msg[80];
    snprintf(msg, sizeof msg, "piped %u bytes to %u peer(s)", (unsigned)sent, nrx);
    send_pipe();
    httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static void pipe_worker(void *arg) {
    (void)arg;
    httpd_req_t *req;
    for (;;) {
        if (xQueueReceive(s_pipe_jobs, &req, portMAX_DELAY) != pdTRUE) continue;
        stream_put_run(req);
        (void)httpd_req_async_handler_complete(req);
        ESP_LOGI(TAG, "pipe worker done");
    }
}

static esp_err_t stream_put(httpd_req_t *req) {
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "async");
        return ESP_OK;
    }
    if (!s_pipe_jobs || xQueueSend(s_pipe_jobs, &copy, 0) != pdTRUE) {
        httpd_resp_send_err(copy, HTTPD_500_INTERNAL_SERVER_ERROR, "pipe busy");
        (void)httpd_req_async_handler_complete(copy);
        return ESP_OK;
    }
    return ESP_OK;
}

static void http_start(void) {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 24;
    /* LWIP sockets default to 10; httpd needs max_open_sockets+3. 10 fails. */
    cfg.max_open_sockets = 7;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 8;
    cfg.send_wait_timeout = 8;
    esp_err_t he = httpd_start(&s_httpd, &cfg);
    if (he != ESP_OK) {
        ESP_LOGE(TAG, "httpd start %s", esp_err_to_name(he));
        s_httpd = NULL;
        bn_puts("BN AP http fail\n");
        return;
    }
    ESP_LOGI(TAG, "httpd :80");
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = idx_get },
        { .uri = "/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/files", .method = HTTP_GET, .handler = files_get },
        { .uri = "/files/*", .method = HTTP_GET, .handler = files_get },
        { .uri = "/file", .method = HTTP_GET, .handler = file_get },
        { .uri = "/put/*", .method = HTTP_PUT, .handler = put_handler },
        { .uri = "/put/*", .method = HTTP_POST, .handler = put_handler },
        { .uri = "/board", .method = HTTP_GET, .handler = board_get },
        { .uri = "/board", .method = HTTP_POST, .handler = board_post },
        { .uri = "/peers", .method = HTTP_GET, .handler = peers_get },
        { .uri = "/pipe/arm", .method = HTTP_POST, .handler = pipe_arm },
        { .uri = "/pipe/c/*", .method = HTTP_GET, .handler = pipe_chunk },
        { .uri = "/pipe/x/*", .method = HTTP_POST, .handler = pipe_cancel },
        { .uri = "/stream", .method = HTTP_PUT, .handler = stream_put },
        { .uri = "/stream", .method = HTTP_POST, .handler = stream_put },
        { .uri = "/stream/*", .method = HTTP_PUT, .handler = stream_put },
        { .uri = "/stream/*", .method = HTTP_POST, .handler = stream_put },
    };
    for (unsigned i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
}

static void http_stop(void) {
    pipe_abort();
    if (!s_httpd) return;
    httpd_stop(s_httpd);
    s_httpd = NULL;
}

static void ap_force_legacy(void);

static void wifi_ev(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_AP_START) {
        ap_force_legacy();
        return;
    }
    if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_sta_list_t list;
        memset(&list, 0, sizeof list);
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
            s_clients = (uint8_t)list.num;
        }
        if (id == WIFI_EVENT_AP_STADISCONNECTED && (s_pipe_put || s_nrx)) {
            ESP_LOGW(TAG, "sta left, drop pipe clients=%u nrx=%u",
                     (unsigned)s_clients, s_nrx);
            pipe_abort();
        }
        send_sta();
    }
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

static void apply_ap_cfg(void) {
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.ap.ssid, "FWOG-ferry", 10);
    cfg.ap.ssid_len = 10;
    cfg.ap.ssid_hidden = 0;
    cfg.ap.channel = 6;
    cfg.ap.beacon_interval = 100;
    cfg.ap.dtim_period = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    cfg.ap.pmf_cfg.required = false;
    memcpy(cfg.ap.password, s_pass, 8);
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

void ferry_ap_start(void) {
    if (s_ap_on) {
        send_ap_on();
        send_sta();
        return;
    }
    ensure_pass();
    apply_ap_cfg();
    ap_force_legacy();
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "wifi start %s", esp_err_to_name(e));
        char line[72];
        snprintf(line, sizeof line, "BN AP fail %s\n", esp_err_to_name(e));
        bn_puts(line);
        bn_puts("BN AP off\n");
        return;
    }
    ap_force_legacy();
    s_ap_on = true;
    http_start();
    send_ap_on();
}

void ferry_ap_stop(void) {
    http_stop();
    esp_wifi_stop();
    s_ap_on = false;
    s_clients = 0;
    bn_puts("BN AP off\n");
}

void ferry_ap_wipe(void) {
    pipe_abort();
    pool_clear();
    fwog_pass_nvs_rotate(FWOG_PASS_KEY_FERRY, s_pass);
    if (s_ap_on) {
        apply_ap_cfg();
        (void)esp_wifi_deauth_sta(0);
        send_ap_on();
    }
    send_sta();
    char line[40];
    snprintf(line, sizeof line, "BN AP wipe pass=%s\n", s_pass);
    bn_puts(line);
}

void ferry_init(void (*puts)(const char *), esp_netif_t *ap_netif) {
    s_puts = puts;
    s_ap_netif = ap_netif;
    (void)s_ap_netif;
    s_pipe_mu = xSemaphoreCreateMutex();
    s_pipe_ev = xSemaphoreCreateBinary();
    s_pipe_jobs = xQueueCreate(1, sizeof(httpd_req_t *));
    xTaskCreate(pipe_worker, "ferry_pipe", 6144, NULL, 4, NULL);
    ensure_pass();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ev, NULL, NULL));
}
