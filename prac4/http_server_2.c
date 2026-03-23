/*
 * HTTP Appointment Server   Practical Assignment 4
 *
 * Demonstrates HTTP/1.1 per RFC 2616:
 *   GET  with query-string parsing (?key=val&…, URL-decode)
 *   POST with multipart/form-data  (binary file upload, RFC 2046)
 *   Status lines: 200, 204, 302, 400, 403, 404, 405, 413
 *   Content-Type, Content-Length, Location, Connection, Cache-Control headers
 *   Binary image serving
 *   302 Post/Redirect/Get pattern (no duplicate-submit on F5)
 *   fork() per connection, SIGCHLD reaping
 *
 * Compile:  gcc -Wall -o http_server http_server_2.c
 * Run:      ./http_server
 * Browse:   http://localhost:8080/
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

#define PORT        8080
#define DB_FILE     "appointments.db"
#define IMG_DIR     "images"
#define BACKLOG     20
#define HDR_BUFSZ   8192
#define MAX_BODY    (4*1024*1024)   //4 MB upload cap


//how each appointment is stored
typedef struct {
    int  id;
    char date[11];      //YYYY-MM-DD\0 
    char time_s[6];     //HH:MM\0
    char with_s[64];
    char note[128];
    char img_ext[8];    // ".jpg" / ".png" / "" if no image 
} Appointment;

//io operations

static void send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return;
        p += n; 
        len -= (size_t)n;
    }
}

// Read until \r\n\r\n appears (carriage return)
static int read_headers(int fd, char *buf, int bufsz) {
    int total = 0;
    while (total < bufsz - 1) {
        ssize_t n = read(fd, buf + total, (size_t)(bufsz - 1 - total));
        if (n <= 0) break;
        total += (int)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return total;
}

/*
 * Read exactly `need` bytes from fd into a fresh malloc buffer.
 * `pre` / `already` are bytes already read (tail of header buffer).
 * Caller must free().
 */
static char *read_body(int fd, size_t need, const char *pre, size_t already) {
    if (need > MAX_BODY) return NULL;
    char *body = malloc(need + 1);
    if (!body) return NULL;
    size_t got = already < need ? already : need;
    if (got) memcpy(body, pre, got);
    while (got < need) {
        ssize_t n = read(fd, body + got, need - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    body[got] = '\0';
    return body;
}


static void url_decode(const char *src, char *dst, size_t dstsz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di < dstsz - 1; i++) {
        if (src[i] == '%' &&
            isxdigit((unsigned char)src[i+1]) &&
            isxdigit((unsigned char)src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static void get_param(const char *qs, const char *name, char *out, size_t outsz) {
    out[0] = '\0';
    if (!qs || !*qs) return;
    size_t nlen = strlen(name);
    const char *p = qs;
    while (p && *p) {
        while (*p == '&') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            char tmp[2048] = {0};
            if (vlen >= sizeof(tmp)) vlen = sizeof(tmp)-1;
            memcpy(tmp, val, vlen);
            url_decode(tmp, out, outsz);
            return;
        }
        p = strchr(p, '&');
    }
}

//form parser
typedef struct {
    char   name[64];
    char   filename[128];
    char   content_type[64];
    char  *data;     
    size_t data_len;
} MpPart;

#define MAX_PARTS 16
typedef struct { MpPart parts[MAX_PARTS]; int count; } Multipart;

static int parse_boundary(const char *ct, char *out, size_t outsz) {
    const char *p = strstr(ct, "boundary=");
    if (!p) return 0;
    p += 9;
    if (*p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != '\r' && *p != '\n' && i < outsz-1)
        out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

/* Extract  name="value"  from a header string. */
static void mp_attr(const char *hdr, const char *attr, char *out, size_t outsz) {
    out[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", attr);
    const char *p = strstr(hdr, needle);
    if (!p) return;
    p += strlen(needle);
    int quoted = (*p == '"');
    if (quoted) p++;
    size_t i = 0;
    while (*p && i < outsz-1) {
        if (quoted  && *p == '"') break;
        if (!quoted && (*p==';'||*p=='\r'||*p=='\n'||*p==' ')) break;
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static void parse_multipart(char *body, size_t body_len,
                              const char *boundary, Multipart *mp) {
    mp->count = 0;

    // delimiter we search for between parts: \r\n--boundary
    char delim[256];
    snprintf(delim, sizeof(delim), "\r\n--%s", boundary);
    size_t dlen = strlen(delim);

    // the body starts with --boundary\r\n  (no leading \r\n)
    char first[256];
    snprintf(first, sizeof(first), "--%s\r\n", boundary);
    char *pos = memmem(body, body_len, first, strlen(first));
    if (!pos) return;
    pos += strlen(first);   // pos now at first part's headers

    while (mp->count < MAX_PARTS) {
        /* find end of this part's headers */
        char *hdr_end = memmem(pos, body_len - (size_t)(pos - body), "\r\n\r\n", 4);
        if (!hdr_end) break;

        size_t hlen = (size_t)(hdr_end - pos);
        char hdrs[1024] = {0};
        if (hlen < sizeof(hdrs)) { memcpy(hdrs, pos, hlen); hdrs[hlen]='\0'; }

        MpPart *part = &mp->parts[mp->count];
        memset(part, 0, sizeof(*part));

        char *cd = strstr(hdrs, "Content-Disposition:");
        if (cd) {
            mp_attr(cd, "name",     part->name,     sizeof(part->name));
            mp_attr(cd, "filename", part->filename, sizeof(part->filename));
        }
        char *ct = strstr(hdrs, "Content-Type:");
        if (ct) {
            ct += 13; while (*ct==' ') ct++;
            size_t i = 0;
            while (*ct && *ct!='\r' && *ct!='\n' && i<sizeof(part->content_type)-1)
                part->content_type[i++] = *ct++;
            part->content_type[i] = '\0';
        }

        char *data_start = hdr_end + 4;
        char *data_end   = memmem(data_start,
                                   body_len - (size_t)(data_start - body),
                                   delim, dlen);
        if (!data_end) break;

        part->data     = data_start;
        part->data_len = (size_t)(data_end - data_start);
        mp->count++;

        char *after = data_end + dlen;
        if (after[0]=='-' && after[1]=='-') break;  /* final --boundary-- */
        if (after[0]=='\r' && after[1]=='\n') after += 2;
        pos = after;
    }
}

static MpPart *mp_find(Multipart *mp, const char *name) {
    for (int i = 0; i < mp->count; i++)
        if (strcmp(mp->parts[i].name, name) == 0)
            return &mp->parts[i];
    return NULL;
}

//identify mime by using 'magic bytes' since actual type can be spoofed or incorrect
static const char *sniff_mime(const char *d, size_t len) {
    if (len>=3 &&
        (unsigned char)d[0]==0xFF &&
        (unsigned char)d[1]==0xD8 &&
        (unsigned char)d[2]==0xFF) return "image/jpeg";

    if (len>=8 &&
        (unsigned char)d[0]==0x89 &&
        d[1]=='P' && d[2]=='N' && d[3]=='G') return "image/png";

    if (len>=6 &&
        (memcmp(d,"GIF87a",6)==0 ||
         memcmp(d,"GIF89a",6)==0)) return "image/gif";

    if (len>=12 &&
        memcmp(d,"RIFF",4)==0 &&
        memcmp(d+8,"WEBP",4)==0) return "image/webp";

    return NULL;
}

static const char *mime_to_ext(const char *mime) {
    if (!mime)             return "";
    if (strstr(mime,"jpeg")) return ".jpg";
    if (strstr(mime,"png"))  return ".png";
    if (strstr(mime,"gif"))  return ".gif";
    if (strstr(mime,"webp")) return ".webp";
    return "";
}

static int count_appointments(void) {
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    int n = (int)(ftell(f) / (long)sizeof(Appointment));
    fclose(f);
    return n;
}

static void db_add(Appointment *a) {
    FILE *f = fopen(DB_FILE, "ab");
    if (!f) return;
    fwrite(a, sizeof(*a), 1, f);
    fclose(f);
}

static void db_delete(int tid) {
    FILE *f = fopen(DB_FILE, "rb"); if (!f) return;
    FILE *t = fopen("appointments.tmp", "wb"); if (!t){fclose(f);return;}
    Appointment a;
    while (fread(&a, sizeof(a), 1, f) == 1) {
        if (a.id == tid) {
            if (a.img_ext[0]) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%d%s", IMG_DIR, a.id, a.img_ext);
                unlink(path);
            }
        } else {
            fwrite(&a, sizeof(a), 1, t);
        }
    }
    fclose(f); fclose(t);
    rename("appointments.tmp", DB_FILE);
}

static Appointment *db_load_all(int *count) {
    *count = count_appointments();
    if (*count == 0) return NULL;
    Appointment *arr = malloc(*count * sizeof(Appointment));
    if (!arr) { *count=0; return NULL; }
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) { free(arr); *count=0; return NULL; }
    int i=0; Appointment a;
    while (i < *count && fread(&a, sizeof(a), 1, f)==1) arr[i++]=a;
    fclose(f); *count=i;
    return arr;
}


static void http_response(int fd,
                           const char *status, const char *ctype,
                           const char *extra,
                           const void *body, size_t blen) {
    char hdr[1024];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        status, ctype, blen, extra ? extra : "");
    send_all(fd, hdr, (size_t)hl);
    if (body && blen) send_all(fd, body, blen);
}

static void http_redirect(int fd, const char *loc) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        loc);
    send_all(fd, hdr, (size_t)hl);
}


typedef struct { char *data; size_t len, cap; } StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap=8192; sb->len=0;
    sb->data=malloc(sb->cap);
    if (sb->data) sb->data[0]='\0';
}
static void sb_grow(StrBuf *sb, size_t need) {
    while (sb->len+need+1>sb->cap) { sb->cap*=2; sb->data=realloc(sb->data,sb->cap); }
}
static void sb_append(StrBuf *sb, const char *s) {
    size_t sl=strlen(s); sb_grow(sb,sl);
    memcpy(sb->data+sb->len,s,sl+1); sb->len+=sl;
}
static void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    char tmp[4096]; va_list ap;
    va_start(ap,fmt); vsnprintf(tmp,sizeof(tmp),fmt,ap); va_end(ap);
    sb_append(sb,tmp);
}
static void html_esc(const char *s, char *d, size_t dsz) {
    size_t di=0;
    for(;*s&&di<dsz-7;s++){
        if     (*s=='<'){memcpy(d+di,"&lt;",  4);di+=4;}
        else if(*s=='>'){memcpy(d+di,"&gt;",  4);di+=4;}
        else if(*s=='&'){memcpy(d+di,"&amp;", 5);di+=5;}
        else if(*s=='"'){memcpy(d+di,"&quot;",6);di+=6;}
        else            {d[di++]=*s;}
    }
    d[di]='\0';
}


//styling totally not stolen
static const char *CSS =
"<style>"
"@import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono"
"&family=Orbitron:wght@400;700&display=swap');"
":root{"
"  --bg:#0d0f14;--panel:#141720;--border:#2a3a2a;"
"  --accent:#39ff14;--accent2:#00d4ff;--warn:#ff4444;"
"  --text:#c8ffc8;--dim:#5a7a5a;--r:4px}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:var(--bg);color:var(--text);"
"  font-family:'Share Tech Mono',monospace;min-height:100vh;padding-bottom:60px}"
"body::before{content:'';pointer-events:none;position:fixed;inset:0;"
"  background:repeating-linear-gradient(0deg,transparent,transparent 2px,"
"  rgba(0,0,0,.18) 2px,rgba(0,0,0,.18) 4px);z-index:9999}"
"header{background:var(--panel);border-bottom:2px solid var(--accent);"
"  padding:18px 40px;display:flex;align-items:center}"
"h1{font-family:'Orbitron',sans-serif;font-size:1.4rem;color:var(--accent);"
"  letter-spacing:.15em;text-shadow:0 0 12px var(--accent)}"
".sub{color:var(--dim);font-size:.75rem;letter-spacing:.1em;margin-top:4px}"
"nav{background:var(--panel);border-bottom:1px solid var(--border);padding:0 40px;display:flex}"
"nav a{display:inline-block;padding:12px 22px;color:var(--dim);text-decoration:none;"
"  font-size:.8rem;letter-spacing:.08em;border-bottom:3px solid transparent;transition:all .15s}"
"nav a:hover,nav a.active{color:var(--accent);border-bottom-color:var(--accent);"
"  text-shadow:0 0 8px var(--accent)}"
".container{max-width:960px;margin:0 auto;padding:36px 24px}"
".panel{background:var(--panel);border:1px solid var(--border);"
"  border-radius:var(--r);padding:28px;margin-bottom:24px}"
".ptitle{font-family:'Orbitron',sans-serif;font-size:.85rem;color:var(--accent2);"
"  letter-spacing:.12em;margin-bottom:22px;padding-bottom:10px;border-bottom:1px solid var(--border)}"
"label{display:block;font-size:.75rem;color:var(--dim);letter-spacing:.08em;"
"  margin-bottom:5px;margin-top:14px}"
"input[type=text],input[type=date],input[type=time],input[type=file]{"
"  width:100%;background:#0a0c10;border:1px solid var(--border);"
"  border-radius:var(--r);color:var(--text);font-family:'Share Tech Mono',monospace;"
"  font-size:.9rem;padding:9px 12px;outline:none;transition:border-color .15s,box-shadow .15s}"
"input[type=file]{padding:7px 12px;cursor:pointer}"
"input:focus{border-color:var(--accent);box-shadow:0 0 0 2px rgba(57,255,20,.15)}"
".btn{display:inline-block;margin-top:20px;padding:10px 28px;background:transparent;"
"  border:1px solid var(--accent);color:var(--accent);font-family:'Share Tech Mono',monospace;"
"  font-size:.85rem;letter-spacing:.1em;cursor:pointer;border-radius:var(--r);"
"  transition:all .15s;text-shadow:0 0 6px var(--accent);text-decoration:none}"
".btn:hover{background:var(--accent);color:#000;box-shadow:0 0 18px rgba(57,255,20,.4);text-shadow:none}"
".btn-d{border-color:var(--warn);color:var(--warn);text-shadow:0 0 6px var(--warn)}"
".btn-d:hover{background:var(--warn);color:#fff;box-shadow:0 0 18px rgba(255,68,68,.4)}"
"table{width:100%;border-collapse:collapse;font-size:.82rem}"
"th{text-align:left;padding:8px 12px;color:var(--accent2);font-size:.72rem;"
"  letter-spacing:.1em;border-bottom:1px solid var(--accent2)}"
"td{padding:10px 12px;border-bottom:1px solid var(--border);vertical-align:middle}"
"tr:hover td{background:rgba(57,255,20,.04)}"
".idc{color:var(--dim);width:36px}.dc{color:var(--accent2)}.wc{color:var(--accent)}.nc{color:#a0c0a0}"
".av{width:44px;height:44px;border-radius:50%;object-fit:cover;"
"  border:2px solid var(--border);display:block;cursor:zoom-in;transition:border-color .2s}"
".av:hover{border-color:var(--accent)}"
".avp{width:44px;height:44px;border-radius:50%;background:var(--border);"
"  display:flex;align-items:center;justify-content:center;font-size:1.2rem;color:var(--dim)}"
/* image preview on add form */
"#pw{margin-top:14px;display:none}"
"#pw img{max-width:180px;max-height:180px;border-radius:var(--r);"
"  border:2px solid var(--accent);box-shadow:0 0 18px rgba(57,255,20,.3)}"
"#pw .pl{font-size:.7rem;color:var(--dim);margin-bottom:6px;letter-spacing:.08em}"
/* lightbox */
"#lb{display:none;position:fixed;inset:0;background:rgba(0,0,0,.88);"
"  z-index:10000;align-items:center;justify-content:center;cursor:zoom-out}"
"#lb.open{display:flex}"
"#lb img{max-width:90vw;max-height:90vh;border-radius:var(--r);"
"  border:2px solid var(--accent);box-shadow:0 0 40px rgba(57,255,20,.3)}"
".df{display:inline}.df button{background:none;border:1px solid transparent;"
"  cursor:pointer;color:var(--warn);font-family:'Share Tech Mono',monospace;"
"  font-size:.75rem;padding:3px 9px;border-radius:var(--r);transition:all .15s}"
".df button:hover{border-color:var(--warn);box-shadow:0 0 8px rgba(255,68,68,.3)}"
".empty{color:var(--dim);font-size:.8rem;padding:16px 0}"
".ok{background:rgba(57,255,20,.08);border:1px solid rgba(57,255,20,.3);"
"  color:var(--accent);padding:10px 16px;border-radius:var(--r);margin-bottom:18px;font-size:.82rem}"
".er{background:rgba(255,68,68,.08);border:1px solid rgba(255,68,68,.3);"
"  color:var(--warn);padding:10px 16px;border-radius:var(--r);margin-bottom:18px;font-size:.82rem}"
".g2{display:grid;grid-template-columns:1fr 1fr;gap:0 24px}"
"@media(max-width:600px){.g2{grid-template-columns:1fr}}"
"footer{text-align:center;color:var(--dim);font-size:.65rem;letter-spacing:.08em;padding:30px}"
"</style>";


static void page_header(StrBuf *sb, const char *active) {
    sb_append(sb,
        "<!DOCTYPE html><html lang='en'>"
        "<head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>APPT // Manager</title>");
    sb_append(sb, CSS);
    sb_append(sb,
        "</head><body>"
        "<header><div>"
        "<h1>&#9632; APPOINTMENT THING</h1>"
        "<div class='sub'>HTTP/1.1 &middot; appointment tracking system</div>"
        "</div></header><nav>");
    const char *links[][2]={
        {"/","HOME"},{"/add","ADD"},{"/search","SEARCH"},{NULL,NULL}};
    for(int i=0;links[i][0];i++)
        sb_appendf(sb,"<a href='%s' class='%s'>%s</a>",
            links[i][0], strcmp(links[i][0],active)==0?"active":"", links[i][1]);
    sb_append(sb,"</nav><div class='container'>");
}

static void page_footer(StrBuf *sb) {
    sb_append(sb,
        /* lightbox overlay */
        "<div id='lb' onclick='this.classList.remove(\"open\")'>"
        "<img id='lbi' src='' alt=''></div>"
        "<script>"
        "function si(src){document.getElementById('lbi').src=src;"
        "document.getElementById('lb').classList.add('open');}"
        /* live photo preview on add form */
        "function pp(i){var w=document.getElementById('pw'),"
        "img=document.getElementById('pi');"
        "if(i.files&&i.files[0]){"
        "var r=new FileReader();r.onload=function(e){"
        "img.src=e.target.result;w.style.display='block';};"
        "r.readAsDataURL(i.files[0]);}else{w.style.display='none';}}"
        "</script>"
        "</div>" /* .container */
        "<footer>HTTP/1.1 APPOINTMENT SERVER THING &middot; COS332 PRACTICAL 4</footer>"
        "</body></html>");
}

static void build_home(StrBuf *sb, const char *ok, const char *err) {
    page_header(sb, "/");
    sb_append(sb,"<div class='panel'><div class='ptitle'>// ALL APPOINTMENTS</div>");
    if (ok  && *ok)  sb_appendf(sb,"<div class='ok'>&#10003; %s</div>",ok);
    if (err && *err) sb_appendf(sb,"<div class='er'>&#10007; %s</div>",err);

    int count=0;
    Appointment *all=db_load_all(&count);
    if (count==0) {
        sb_append(sb,"<p class='empty'>No appointments yet.</p>"
                     "<a href='/add' class='btn' style='margin-top:12px'>+ ADD APPOINTMENT</a>");
    } else {
        sb_append(sb,
            "<table><thead><tr>"
            "<th></th><th>#</th><th>DATE</th><th>TIME</th>"
            "<th>WITH</th><th>NOTE</th><th></th>"
            "</tr></thead><tbody>");
        for (int i=0;i<count;i++) {
            Appointment *a=&all[i];
            char we[128],ne[256];
            html_esc(a->with_s,we,sizeof(we));
            html_esc(a->note,  ne,sizeof(ne));
            sb_appendf(sb,"<tr>");
            if (a->img_ext[0])
                sb_appendf(sb,
                    "<td><img class='av' src='/img/%d%s' alt='%s'"
                    " onclick=\"si('/img/%d%s')\"></td>",
                    a->id,a->img_ext,we,a->id,a->img_ext);
            else
                sb_append(sb,"<td><div class='avp'>&#128100;</div></td>");
            sb_appendf(sb,
                "<td class='idc'>%d</td>"
                "<td class='dc'>%s</td><td class='dc'>%s</td>"
                "<td class='wc'>%s</td><td class='nc'>%s</td>"
                "<td><form class='df' method='post' action='/delete'>"
                "<input type='hidden' name='id' value='%d'>"
                "<button onclick=\"return confirm('Delete #%d?')\">DEL</button>"
                "</form></td></tr>",
                a->id,a->date,a->time_s,we,ne,a->id,a->id);
        }
        sb_append(sb,"</tbody></table>");
        sb_appendf(sb,"<p style='margin-top:14px;color:var(--dim);font-size:.72rem'>"
                      "%d appointment(s)</p>",count);
    }
    if (all) free(all);
    sb_append(sb,"</div>");
    page_footer(sb);
}

static void build_add_form(StrBuf *sb, const char *err) {
    page_header(sb, "/add");
    sb_append(sb,"<div class='panel'><div class='ptitle'>// ADD APPOINTMENT</div>");
    if (err && *err) sb_appendf(sb,"<div class='er'>&#10007; %s</div>",err);

    /*
     * enctype="multipart/form-data"   required for binary file upload.
     * method="post"   file bytes go in the request body, not the URL.
     * Without multipart encoding the browser would try to URL-encode the
     * binary data and corrupt it.
     */
    sb_append(sb,
        "<form method='post' action='/do_add' enctype='multipart/form-data'>"
        "<div class='g2'>"
        "<div><label>DATE</label><input type='date' name='date' required></div>"
        "<div><label>TIME</label><input type='time' name='time' required></div>"
        "</div>"
        "<label>WITH (person&#39;s name)</label>"
        "<input type='text' name='with' required maxlength='60' placeholder='e.g. Dr. Smith'>"
        "<label>NOTE (optional)</label>"
        "<input type='text' name='note' maxlength='120' placeholder='e.g. Bring documents'>"
        "<label>PHOTO (optional &mdash; JPEG / PNG / GIF / WebP, max 4 MB)</label>"
        "<input type='file' name='photo' accept='image/*' onchange='pp(this)'>"
        "<div id='pw'>"
        "  <div class='pl'>PREVIEW</div>"
        "  <img id='pi' src='' alt='preview'>"
        "</div>"
        "<br><input type='submit' class='btn' value='SAVE APPOINTMENT'>"
        "</form>");
    sb_append(sb,"</div>");
    page_footer(sb);
}

static void build_search(StrBuf *sb, const char *q) {
    page_header(sb, "/search");
    sb_append(sb,"<div class='panel'><div class='ptitle'>// SEARCH</div>");
    char qe[128]="";
    if (q) html_esc(q, qe, sizeof(qe));
    sb_appendf(sb,
        "<form method='get' action='/search'>"
        "<label>SEARCH BY NAME</label>"
        "<input type='text' name='q' value='%s' placeholder='Enter name...' autofocus>"
        "<input type='submit' class='btn' value='SEARCH' style='margin-left:12px'>"
        "</form>",qe);

    if (q && *q) {
        FILE *f=fopen(DB_FILE,"rb");
        int found=0;
        if (f) {
            sb_append(sb,
                "<div style='margin-top:24px'><table><thead><tr>"
                "<th></th><th>#</th><th>DATE</th><th>TIME</th><th>WITH</th><th>NOTE</th>"
                "</tr></thead><tbody>");
            Appointment a;
            while (fread(&a,sizeof(a),1,f)==1) {
                char wl[64],ql[64];
                strncpy(wl,a.with_s,63);wl[63]=0;
                strncpy(ql,q,63);ql[63]=0;
                for(char*p=wl;*p;p++)*p=(char)tolower((unsigned char)*p);
                for(char*p=ql;*p;p++)*p=(char)tolower((unsigned char)*p);
                if (!strstr(wl,ql)) continue;
                char we[128],ne[256];
                html_esc(a.with_s,we,sizeof(we));
                html_esc(a.note,  ne,sizeof(ne));
                if (a.img_ext[0])
                    sb_appendf(sb,
                        "<tr><td><img class='av' src='/img/%d%s' alt='%s'"
                        " onclick=\"si('/img/%d%s')\"></td>",
                        a.id,a.img_ext,we,a.id,a.img_ext);
                else
                    sb_append(sb,"<tr><td><div class='avp'>&#128100;</div></td>");
                sb_appendf(sb,
                    "<td class='idc'>%d</td>"
                    "<td class='dc'>%s</td><td class='dc'>%s</td>"
                    "<td class='wc'>%s</td><td class='nc'>%s</td></tr>",
                    a.id,a.date,a.time_s,we,ne);
                found++;
            }
            fclose(f);
            sb_append(sb,"</tbody></table></div>");
        }
        if (!found)
            sb_appendf(sb,"<p class='empty' style='margin-top:16px'>"
                          "No results for &ldquo;%s&rdquo;.</p>",qe);
        else
            sb_appendf(sb,"<p style='margin-top:10px;color:var(--dim);font-size:.72rem'>"
                          "%d result(s)</p>",found);
    }
    sb_append(sb,"</div>");
    page_footer(sb);
}

static void build_404(StrBuf *sb) {
    page_header(sb,"");
    sb_append(sb,
        "<div class='panel' style='text-align:center;padding:60px'>"
        "<div style='font-family:Orbitron,sans-serif;font-size:4rem;"
        "color:var(--warn);text-shadow:0 0 30px rgba(255,68,68,.5)'>404</div>"
        "<div style='color:var(--dim);margin:16px 0 28px;letter-spacing:.1em;"
        "font-size:.8rem'>RESOURCE NOT FOUND</div>"
        "<a href='/' class='btn'>GO HOME</a></div>");
    page_footer(sb);
}

typedef struct {
    char   method[8];
    char   path[256];
    char   qs[1024];
    char   content_type[256];
    long   content_length;
    char  *body_pre;    //pointer into hdr_buf just past \r\n\r\n 
    size_t body_pre_len;
} Request;

static int parse_request(char *buf, int total, Request *req) {
    
    memset(req,0,sizeof(*req));
    req->content_length=-1;

    char *eol=strstr(buf,"\r\n"); if(!eol) return -1;
    char line[512]; size_t ll=(size_t)(eol-buf);
    if(ll>=sizeof(line)) return -1;
    memcpy(line,buf,ll); line[ll]='\0';

    char *s1=strchr(line,' '); if(!s1) return -1; *s1='\0';
    snprintf(req->method,sizeof(req->method),"%s",line);
    char *url=s1+1;
    char *s2=strchr(url,' '); if(s2)*s2='\0';
    char *qm=strchr(url,'?');
    if(qm){*qm='\0';snprintf(req->qs,sizeof(req->qs),"%s",qm+1);}
    snprintf(req->path,sizeof(req->path),"%s",url);

    char *hdr_end=strstr(buf,"\r\n\r\n"); if(!hdr_end) return -1;
    char *p=eol+2;
    while(p<hdr_end){
        char *le=strstr(p,"\r\n"); if(!le) break;
        size_t hl2=(size_t)(le-p);
        char h[512]={0};
        if(hl2<sizeof(h)){memcpy(h,p,hl2);h[hl2]='\0';}
        if(strncasecmp(h,"Content-Type:",13)==0){
            char *v=h+13;while(*v==' ')v++;
            snprintf(req->content_type,sizeof(req->content_type),"%s",v);}
        if(strncasecmp(h,"Content-Length:",15)==0){
            char *v=h+15;while(*v==' ')v++;
            req->content_length=atol(v);}
        p=le+2;
    }
    req->body_pre=hdr_end+4;
    /* bytes already received beyond the header block */
    char *buf_read_end=buf+total;
    req->body_pre_len=(req->body_pre<buf_read_end)?
                       (size_t)(buf_read_end-req->body_pre):0;

    printf("[%s] %s%s%s (ct=%s cl=%ld)\n",
        req->method,req->path,
        req->qs[0]?"?":"",req->qs[0]?req->qs:"",
        req->content_type[0]?req->content_type:"-",
        req->content_length);
    fflush(stdout);
    return 0;
}

/*
 * GET /img/<id><ext>
 *
 * Reads the image file from disk and sends it with the correct
 * Content-Type derived from magic-byte sniffing.
 * Also sends Cache-Control: max-age=3600 (RFC 2616 §14.9) so the browser
 * does not re-fetch the image on every page load.
 */
static void serve_image(int fd, const char *path) {
    const char *fname = path + 5;   /* skip "/img/" */
    if (strchr(fname,'/')||strchr(fname,'\\')||strstr(fname,"..")) {
        http_response(fd,"403 Forbidden","text/plain",NULL,"Forbidden",9);
        return;
    }
    char fp[256];
    snprintf(fp,sizeof(fp),"%s/%s",IMG_DIR,fname);

    FILE *f=fopen(fp,"rb");
    if (!f){
        http_response(fd,"404 Not Found","text/plain",NULL,"Not found",9);
        return;
    }
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    if(sz<=0||sz>MAX_BODY){fclose(f);return;}

    char *img=malloc((size_t)sz);
    if(!img){fclose(f);return;}
    fread(img,1,(size_t)sz,f); fclose(f);

    const char *mime=sniff_mime(img,(size_t)sz);
    if(!mime) mime="application/octet-stream";

    // Cache-Control lets the browser cache the image for 1 hour 
    http_response(fd,"200 OK",mime,"Cache-Control: max-age=3600\r\n",img,(size_t)sz);
    free(img);
}

/*
 * POST /do_add
 *
 * Reads the multipart body, extracts text fields + optional image,
 * validates, saves image to disk, saves record to DB, redirects.
 */
static void handle_post_add(int fd, Request *req) {
    if (req->content_length<0||req->content_length>MAX_BODY) {
        http_response(fd,"413 Content Too Large","text/plain",NULL,"Body too large",14);
        return;
    }
    char *body=read_body(fd,(size_t)req->content_length,
                          req->body_pre,req->body_pre_len);
    if (!body){
        http_response(fd,"400 Bad Request","text/plain",NULL,"Read error",10);
        return;
    }

    char boundary[128]={0};
    if(!parse_boundary(req->content_type,boundary,sizeof(boundary))){
        free(body);
        http_response(fd,"400 Bad Request","text/plain",NULL,"No boundary",11);
        return;
    }

    Multipart mp;
    parse_multipart(body,(size_t)req->content_length,boundary,&mp);

    MpPart *pd=mp_find(&mp,"date"),
           *pt=mp_find(&mp,"time"),
           *pw=mp_find(&mp,"with"),
           *pn=mp_find(&mp,"note"),
           *pp=mp_find(&mp,"photo");

    if(!pd||!pt||!pw||pd->data_len==0||pt->data_len==0||pw->data_len==0){
        free(body);
        StrBuf sb; sb_init(&sb);
        build_add_form(&sb,"Date, time, and name are required.");
        http_response(fd,"400 Bad Request","text/html; charset=utf-8",NULL,sb.data,sb.len);
        free(sb.data); return;
    }

    Appointment a; memset(&a,0,sizeof(a));
    a.id=count_appointments()+1;

#define CP(dst,part) do{ \
    size_t _n=(part)->data_len<sizeof(dst)-1?(part)->data_len:sizeof(dst)-1; \
    memcpy(dst,(part)->data,_n);dst[_n]='\0'; }while(0)

    CP(a.date,   pd);
    CP(a.time_s, pt);
    CP(a.with_s, pw);
    if(pn&&pn->data_len){ CP(a.note,pn); }
#undef CP

    //optional image
    if (pp && pp->data_len > 0) {
        const char *mime=sniff_mime(pp->data,pp->data_len);
        if (!mime) {
            free(body);
            StrBuf sb; sb_init(&sb);
            build_add_form(&sb,"Unsupported image type. Use JPEG, PNG, GIF, or WebP.");
            http_response(fd,"400 Bad Request","text/html; charset=utf-8",NULL,sb.data,sb.len);
            free(sb.data); return;
        }
        const char *ext=mime_to_ext(mime);
        strncpy(a.img_ext,ext,sizeof(a.img_ext)-1);
        mkdir(IMG_DIR,0755);
        char imgpath[256];
        snprintf(imgpath,sizeof(imgpath),"%s/%d%s",IMG_DIR,a.id,ext);
        FILE *f=fopen(imgpath,"wb");
        if(f){
            fwrite(pp->data,1,pp->data_len,f);
            fclose(f);
            printf("[img] saved %s (%zu bytes, %s)\n",imgpath,pp->data_len,mime);
            fflush(stdout);
        }
    }

    db_add(&a);
    free(body);
    http_redirect(fd,"/?ok=Appointment+saved.");
}

// POST /delete  (id field in application/x-www-form-urlencoded body)
static void handle_post_delete(int fd, Request *req) {
    long cl=req->content_length;
    if(cl<0)cl=0; if(cl>256)cl=256;
    char *body=read_body(fd,(size_t)cl,req->body_pre,req->body_pre_len);
    char ids[16]="0";
    if(body){get_param(body,"id",ids,sizeof(ids));free(body);}
    int id=atoi(ids);
    if(id>0){ db_delete(id); http_redirect(fd,"/?ok=Appointment+deleted."); }
    else     { http_redirect(fd,"/?err=Invalid+ID."); }
}

static void handle_client(int fd) {
    char hdr_buf[HDR_BUFSZ];
    memset(hdr_buf,0,sizeof(hdr_buf));
    int hlen=read_headers(fd,hdr_buf,HDR_BUFSZ);
    if(hlen<=0){close(fd);exit(0);}

    Request req;
    if(parse_request(hdr_buf,hlen,&req)<0){
        http_response(fd,"400 Bad Request","text/plain",NULL,"Bad request",11);
        close(fd);exit(0);
    }

    StrBuf sb; sb_init(&sb);

    /* RFC 2616 §9.4: HEAD is identical to GET but we send no body.
     * We handle it by routing through the same GET logic. */
    int is_head = strcmp(req.method,"HEAD")==0;
    if (strcmp(req.method,"GET")==0 || is_head) {

        if (strcmp(req.path,"/")==0) {
            char ok[128]="",er[128]="";
            get_param(req.qs,"ok", ok,sizeof(ok));
            get_param(req.qs,"err",er,sizeof(er));
            build_home(&sb,ok,er);
            http_response(fd,"200 OK","text/html; charset=utf-8",NULL,sb.data,sb.len);

        } else if (strcmp(req.path,"/add")==0) {
            build_add_form(&sb,NULL);
            http_response(fd,"200 OK","text/html; charset=utf-8",NULL,sb.data,sb.len);

        } else if (strcmp(req.path,"/search")==0) {
            char q[64]=""; get_param(req.qs,"q",q,sizeof(q));
            build_search(&sb,q[0]?q:NULL);
            http_response(fd,"200 OK","text/html; charset=utf-8",NULL,sb.data,sb.len);

        } else if (strncmp(req.path,"/img/",5)==0) {
            serve_image(fd,req.path);

        } else if (strcmp(req.path,"/favicon.ico")==0) {
            send_all(fd,"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n",47);

        } else {
            build_404(&sb);
            http_response(fd,"404 Not Found","text/html; charset=utf-8",NULL,sb.data,sb.len);
        }

    } else if (strcmp(req.method,"POST")==0) {

        if      (strcmp(req.path,"/do_add") ==0) handle_post_add(fd,&req);
        else if (strcmp(req.path,"/delete") ==0) handle_post_delete(fd,&req);
        else {
            build_404(&sb);
            http_response(fd,"404 Not Found","text/html; charset=utf-8",NULL,sb.data,sb.len);
        }

    } else {
        //RFC 2616 §10.4.6 - 405 Must include Allow header
        http_response(fd,"405 Method Not Allowed","text/plain",
                      "Allow: GET, POST\r\n","Method not allowed",18);
    }

    free(sb.data);
    /* shutdown the write half so the OS flushes the send buffer
     * before we exit prevents the client receiving RST instead of FIN */
    shutdown(fd, SHUT_WR);
    close(fd);
    exit(0);
}

static void reap(int sig){(void)sig;while(waitpid(-1,NULL,WNOHANG)>0);}

int main(void) {
    signal(SIGCHLD,reap);
    mkdir(IMG_DIR,0755);

    int sfd=socket(AF_INET,SOCK_STREAM,0);
    if(sfd<0){perror("socket");return 1;}
    int opt=1;
    setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons(PORT);

    if(bind(sfd,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");return 1;}
    if(listen(sfd,BACKLOG)<0){perror("listen");return 1;}

    printf("  Server running on port %d\n",PORT);
    printf("  Open:  http://localhost:%d/\n",PORT);
    fflush(stdout);

    while(1){
        struct sockaddr_in ca; socklen_t clen=sizeof(ca);
        int cfd=accept(sfd,(struct sockaddr*)&ca,&clen);
        if(cfd<0) continue;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ca.sin_addr,ip,sizeof(ip));
        printf("[connect] %s\n",ip); fflush(stdout);
        pid_t pid=fork();
        if(pid<0){perror("fork");close(cfd);continue;}
        if(pid==0){close(sfd);handle_client(cfd);exit(0);}
        close(cfd);
    }
}