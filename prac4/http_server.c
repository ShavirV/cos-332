/*
 *compile: gcc -o http_server http_server.c
 *run:     ./http_server
 *open:    http://localhost:8080/
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

#define PORT 8080
#define DB_FILE "appointments.db"
#define BACKLOG 20
#define BUFSIZE 8192


//data model for each appointment
typedef struct {
    int  id;
    char date[11]; //YYYY-MM-DD\0 
    char time[6];  //HH:MM\0  
    char with[64];
    char note[128];
} Appointment;

// Convert %XX hex sequences and '+' back to plain characters
static void url_decode(const char *src, char *dst, size_t dstsz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di < dstsz - 1; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1])
                          && isxdigit((unsigned char)src[i+2])) {
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

// Extract the value of a named parameter from a query string. e.g. get_param("date=2025-01-01&with=Bob", "with", buf, 64) -> "Bob"
static void get_param(const char *qs, const char *name, char *out, size_t outsz) {
    out[0] = '\0';
    size_t nlen = strlen(name);
    const char *p = qs;
    while (p && *p) {
        /* skip '&' separators */
        while (*p == '&') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            if (vlen >= outsz) vlen = outsz - 1;
            char tmp[1024] = {0};
            if (vlen < sizeof(tmp)) {
                memcpy(tmp, val, vlen);
                tmp[vlen] = '\0';
                url_decode(tmp, out, outsz);
            }
            return;
        }
        /* advance past this key=value pair */
        p = strchr(p, '&');
    }
}

/* ─── Database helpers ───────────────────────────────────────────────────── */

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

static void db_delete(int target_id) {
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) return;
    FILE *t = fopen("appointments.tmp", "wb");
    if (!t) { fclose(f); return; }
    Appointment a;
    while (fread(&a, sizeof(a), 1, f) == 1)
        if (a.id != target_id)
            fwrite(&a, sizeof(a), 1, t);
    fclose(f); fclose(t);
    rename("appointments.tmp", DB_FILE);
}

/* Load all appointments into a malloc'd array; *count set to size. Caller frees. */
static Appointment *db_load_all(int *count) {
    *count = count_appointments();
    if (*count == 0) return NULL;
    Appointment *arr = malloc(*count * sizeof(Appointment));
    if (!arr) { *count = 0; return NULL; }
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) { free(arr); *count = 0; return NULL; }
    int i = 0;
    Appointment a;
    while (i < *count && fread(&a, sizeof(a), 1, f) == 1)
        arr[i++] = a;
    fclose(f);
    *count = i;
    return arr;
}

/* ─── HTTP helpers ───────────────────────────────────────────────────────── */

/* Send a raw buffer over a socket, handling short writes */
static void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) break;
        sent += n;
    }
}

/* Send a complete HTTP response.
   status_line  e.g. "200 OK"
   extra_headers  additional header lines, each ending \r\n (may be NULL)
   body / body_len  the response body */
static void http_response(int fd,
                           const char *status_line,
                           const char *content_type,
                           const char *extra_headers,
                           const char *body,
                           size_t body_len)
{
    char hdr[1024];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status_line,
        content_type,
        body_len,
        extra_headers ? extra_headers : "");
    send_all(fd, hdr, (size_t)hlen);
    if (body && body_len > 0)
        send_all(fd, body, body_len);
}

/* Send a 302 redirect */
static void http_redirect(int fd, const char *location) {
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
    send_all(fd, hdr, (size_t)hlen);
}

/* ─── HTML page builder ──────────────────────────────────────────────────── */

/*
 * We dynamically build HTML in a growable string buffer.
 */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap  = 4096;
    sb->len  = 0;
    sb->data = malloc(sb->cap);
    if (sb->data) sb->data[0] = '\0';
}

static void sb_append(StrBuf *sb, const char *s) {
    size_t slen = strlen(s);
    while (sb->len + slen + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, slen + 1);
    sb->len += slen;
}

static void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_append(sb, tmp);
}

/* HTML-escape a string for safe display */
static void html_escape(const char *src, char *dst, size_t dstsz) {
    size_t di = 0;
    for (; *src && di < dstsz - 6; src++) {
        if      (*src == '<')  { memcpy(dst+di, "&lt;",  4); di += 4; }
        else if (*src == '>')  { memcpy(dst+di, "&gt;",  4); di += 4; }
        else if (*src == '&')  { memcpy(dst+di, "&amp;", 5); di += 5; }
        else if (*src == '"')  { memcpy(dst+di, "&quot;",6); di += 6; }
        else                   { dst[di++] = *src; }
    }
    dst[di] = '\0';
}

/* Common page CSS — retro terminal aesthetic */
static const char *PAGE_CSS =
"<style>"
"@import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700&display=swap');"
":root{"
"  --bg:#0d0f14;"
"  --panel:#141720;"
"  --border:#2a3a2a;"
"  --accent:#39ff14;"
"  --accent2:#00d4ff;"
"  --warn:#ff4444;"
"  --text:#c8ffc8;"
"  --dim:#5a7a5a;"
"  --radius:4px;"
"}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{"
"  background:var(--bg);"
"  color:var(--text);"
"  font-family:'Share Tech Mono',monospace;"
"  min-height:100vh;"
"  padding:0 0 60px 0;"
"}"
"/* scanline effect */"
"body::before{"
"  content:'';"
"  pointer-events:none;"
"  position:fixed;inset:0;"
"  background:repeating-linear-gradient("
"    0deg,transparent,transparent 2px,"
"    rgba(0,0,0,.18) 2px,rgba(0,0,0,.18) 4px);"
"  z-index:9999;"
"}"
"header{"
"  background:var(--panel);"
"  border-bottom:2px solid var(--accent);"
"  padding:18px 40px;"
"  display:flex;align-items:center;gap:18px;"
"}"
"header h1{"
"  font-family:'Orbitron',sans-serif;"
"  font-size:1.4rem;"
"  color:var(--accent);"
"  letter-spacing:.15em;"
"  text-shadow:0 0 12px var(--accent);"
"}"
"header .sub{"
"  color:var(--dim);"
"  font-size:.75rem;"
"  letter-spacing:.1em;"
"}"
"nav{"
"  background:var(--panel);"
"  border-bottom:1px solid var(--border);"
"  padding:0 40px;"
"  display:flex;gap:0;"
"}"
"nav a{"
"  display:inline-block;"
"  padding:12px 22px;"
"  color:var(--dim);"
"  text-decoration:none;"
"  font-size:.8rem;"
"  letter-spacing:.08em;"
"  border-bottom:3px solid transparent;"
"  transition:all .15s;"
"}"
"nav a:hover,nav a.active{"
"  color:var(--accent);"
"  border-bottom-color:var(--accent);"
"  text-shadow:0 0 8px var(--accent);"
"}"
".container{max-width:900px;margin:0 auto;padding:36px 24px;}"
".panel{"
"  background:var(--panel);"
"  border:1px solid var(--border);"
"  border-radius:var(--radius);"
"  padding:28px;"
"  margin-bottom:24px;"
"}"
".panel-title{"
"  font-family:'Orbitron',sans-serif;"
"  font-size:.85rem;"
"  color:var(--accent2);"
"  letter-spacing:.12em;"
"  margin-bottom:22px;"
"  padding-bottom:10px;"
"  border-bottom:1px solid var(--border);"
"}"
"label{"
"  display:block;"
"  font-size:.75rem;"
"  color:var(--dim);"
"  letter-spacing:.08em;"
"  margin-bottom:5px;"
"  margin-top:14px;"
"}"
"input[type=text],input[type=date],input[type=time],textarea{"
"  width:100%;"
"  background:#0a0c10;"
"  border:1px solid var(--border);"
"  border-radius:var(--radius);"
"  color:var(--text);"
"  font-family:'Share Tech Mono',monospace;"
"  font-size:.9rem;"
"  padding:9px 12px;"
"  outline:none;"
"  transition:border-color .15s,box-shadow .15s;"
"}"
"input:focus,textarea:focus{"
"  border-color:var(--accent);"
"  box-shadow:0 0 0 2px rgba(57,255,20,.15);"
"}"
".btn{"
"  display:inline-block;"
"  margin-top:20px;"
"  padding:10px 28px;"
"  background:transparent;"
"  border:1px solid var(--accent);"
"  color:var(--accent);"
"  font-family:'Share Tech Mono',monospace;"
"  font-size:.85rem;"
"  letter-spacing:.1em;"
"  cursor:pointer;"
"  border-radius:var(--radius);"
"  transition:all .15s;"
"  text-shadow:0 0 6px var(--accent);"
"  box-shadow:0 0 0 transparent;"
"  text-decoration:none;"
"}"
".btn:hover{"
"  background:var(--accent);"
"  color:#000;"
"  box-shadow:0 0 18px rgba(57,255,20,.4);"
"  text-shadow:none;"
"}"
".btn-danger{"
"  border-color:var(--warn);"
"  color:var(--warn);"
"  text-shadow:0 0 6px var(--warn);"
"}"
".btn-danger:hover{"
"  background:var(--warn);"
"  color:#fff;"
"  box-shadow:0 0 18px rgba(255,68,68,.4);"
"}"
"table{width:100%;border-collapse:collapse;font-size:.82rem;}"
"th{"
"  text-align:left;"
"  padding:8px 12px;"
"  color:var(--accent2);"
"  font-size:.72rem;"
"  letter-spacing:.1em;"
"  border-bottom:1px solid var(--accent2);"
"}"
"td{"
"  padding:10px 12px;"
"  border-bottom:1px solid var(--border);"
"  vertical-align:middle;"
"}"
"tr:hover td{background:rgba(57,255,20,.04);}"
".id-col{color:var(--dim);width:40px;}"
".date-col{color:var(--accent2);}"
".with-col{color:var(--accent);}"
".note-col{color:#a0c0a0;}"
".del-form{display:inline;}"
".del-form button{"
"  background:none;border:none;"
"  cursor:pointer;"
"  color:var(--warn);"
"  font-family:'Share Tech Mono',monospace;"
"  font-size:.75rem;"
"  padding:2px 8px;"
"  border:1px solid transparent;"
"  border-radius:var(--radius);"
"  transition:all .15s;"
"}"
".del-form button:hover{"
"  border-color:var(--warn);"
"  box-shadow:0 0 8px rgba(255,68,68,.3);"
"}"
".empty{color:var(--dim);font-size:.8rem;padding:16px 0;}"
".msg-ok{"
"  background:rgba(57,255,20,.08);"
"  border:1px solid rgba(57,255,20,.3);"
"  color:var(--accent);"
"  padding:10px 16px;"
"  border-radius:var(--radius);"
"  margin-bottom:18px;"
"  font-size:.82rem;"
"}"
".msg-err{"
"  background:rgba(255,68,68,.08);"
"  border:1px solid rgba(255,68,68,.3);"
"  color:var(--warn);"
"  padding:10px 16px;"
"  border-radius:var(--radius);"
"  margin-bottom:18px;"
"  font-size:.82rem;"
"}"
".grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 24px;}"
"@media(max-width:600px){.grid2{grid-template-columns:1fr;}}"
"footer{"
"  text-align:center;"
"  color:var(--dim);"
"  font-size:.65rem;"
"  letter-spacing:.08em;"
"  padding:30px;"
"}"
"</style>";

/* Emit the page header (HTML open + header + nav) */
static void page_header(StrBuf *sb, const char *active) {
    sb_append(sb,
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>APPT // Manager</title>"
    );
    sb_append(sb, PAGE_CSS);
    sb_append(sb, "</head><body>");
    sb_append(sb,
        "<header>"
        "<div>"
        "<h1>&#9632; APPT MANAGER</h1>"
        "<div class='sub'>HTTP/1.1 &middot; appointment tracking system</div>"
        "</div>"
        "</header>"
        "<nav>"
    );

    /* nav links */
    const char *links[][2] = {
        { "/",        "HOME"   },
        { "/add",     "ADD"    },
        { "/search",  "SEARCH" },
        { NULL, NULL }
    };
    for (int i = 0; links[i][0]; i++) {
        sb_appendf(sb,
            "<a href='%s' class='%s'>%s</a>",
            links[i][0],
            strcmp(links[i][0], active) == 0 ? "active" : "",
            links[i][1]
        );
    }
    sb_append(sb, "</nav><div class='container'>");
}

static void page_footer(StrBuf *sb) {
    sb_append(sb,
        "</div>"
        "<footer>HTTP/1.1 APPOINTMENT SERVER &middot; COS301 PRACTICAL 4</footer>"
        "</body></html>"
    );
}

/* ─── Page renderers ─────────────────────────────────────────────────────── */

static void build_home(StrBuf *sb, const char *msg_ok, const char *msg_err) {
    page_header(sb, "/");
    sb_append(sb, "<div class='panel'>");
    sb_append(sb, "<div class='panel-title'>// ALL APPOINTMENTS</div>");

    if (msg_ok && *msg_ok)
        sb_appendf(sb, "<div class='msg-ok'>&#10003; %s</div>", msg_ok);
    if (msg_err && *msg_err)
        sb_appendf(sb, "<div class='msg-err'>&#10007; %s</div>", msg_err);

    int count = 0;
    Appointment *all = db_load_all(&count);

    if (count == 0) {
        sb_append(sb, "<p class='empty'>No appointments yet. </p>"
                      "<a href='/add' class='btn' style='margin-top:0'>+ ADD APPOINTMENT</a>");
    } else {
        sb_append(sb,
            "<table><thead><tr>"
            "<th>#</th><th>DATE</th><th>TIME</th><th>WITH</th><th>NOTE</th><th></th>"
            "</tr></thead><tbody>");
        for (int i = 0; i < count; i++) {
            Appointment *a = &all[i];
            char with_e[128], note_e[256];
            html_escape(a->with, with_e, sizeof(with_e));
            html_escape(a->note, note_e, sizeof(note_e));
            sb_appendf(sb,
                "<tr>"
                "<td class='id-col'>%d</td>"
                "<td class='date-col'>%s</td>"
                "<td class='date-col'>%s</td>"
                "<td class='with-col'>%s</td>"
                "<td class='note-col'>%s</td>"
                "<td>"
                "  <form class='del-form' method='get' action='/delete'>"
                "    <input type='hidden' name='id' value='%d'>"
                "    <button type='submit' onclick=\"return confirm('Delete appointment #%d?')\">DEL</button>"
                "  </form>"
                "</td>"
                "</tr>",
                a->id, a->date, a->time, with_e, note_e, a->id, a->id
            );
        }
        sb_append(sb, "</tbody></table>");
        sb_appendf(sb, "<p style='margin-top:14px;color:var(--dim);font-size:.72rem;'>%d appointment(s) total</p>", count);
    }
    if (all) free(all);

    sb_append(sb, "</div>");
    page_footer(sb);
}

static void build_add_form(StrBuf *sb, const char *err) {
    page_header(sb, "/add");
    sb_append(sb, "<div class='panel'>");
    sb_append(sb, "<div class='panel-title'>// ADD APPOINTMENT</div>");

    if (err && *err)
        sb_appendf(sb, "<div class='msg-err'>&#10007; %s</div>", err);

    sb_append(sb,
        "<form method='get' action='/do_add'>"
        "<div class='grid2'>"
        "  <div>"
        "    <label>DATE (YYYY-MM-DD)</label>"
        "    <input type='date' name='date' required placeholder='2025-06-15'>"
        "  </div>"
        "  <div>"
        "    <label>TIME (HH:MM)</label>"
        "    <input type='time' name='time' required placeholder='14:30'>"
        "  </div>"
        "</div>"
        "<label>WITH (person's name)</label>"
        "<input type='text' name='with' required maxlength='60' placeholder='e.g. Dr. Smith'>"
        "<label>NOTE (optional)</label>"
        "<input type='text' name='note' maxlength='120' placeholder='e.g. Bring documents'>"
        "<br>"
        "<input type='submit' class='btn' value='SAVE APPOINTMENT'>"
        "</form>"
    );
    sb_append(sb, "</div>");
    page_footer(sb);
}

static void build_search_form(StrBuf *sb, const char *query) {
    page_header(sb, "/search");
    sb_append(sb, "<div class='panel'>");
    sb_append(sb, "<div class='panel-title'>// SEARCH APPOINTMENTS</div>");

    char qe[128] = "";
    if (query) html_escape(query, qe, sizeof(qe));

    sb_appendf(sb,
        "<form method='get' action='/search'>"
        "<label>SEARCH BY NAME (\"with\" field)</label>"
        "<input type='text' name='q' value='%s' placeholder='Enter name...' autofocus>"
        "<input type='submit' class='btn' value='SEARCH' style='margin-left:12px;margin-top:20px;'>"
        "</form>",
        qe
    );

    /* show results if query provided */
    if (query && *query) {
        FILE *f = fopen(DB_FILE, "rb");
        int found = 0;
        if (f) {
            sb_append(sb,
                "<div style='margin-top:24px'>"
                "<table><thead><tr>"
                "<th>#</th><th>DATE</th><th>TIME</th><th>WITH</th><th>NOTE</th>"
                "</tr></thead><tbody>");
            Appointment a;
            while (fread(&a, sizeof(a), 1, f) == 1) {
                /* case-insensitive search */
                char wlower[64], qlower[64];
                strncpy(wlower, a.with, sizeof(wlower)-1); wlower[sizeof(wlower)-1]=0;
                strncpy(qlower, query, sizeof(qlower)-1);  qlower[sizeof(qlower)-1]=0;
                for (char *p = wlower; *p; p++) *p = (char)tolower((unsigned char)*p);
                for (char *p = qlower; *p; p++) *p = (char)tolower((unsigned char)*p);

                if (strstr(wlower, qlower)) {
                    char we[128], ne[256];
                    html_escape(a.with, we, sizeof(we));
                    html_escape(a.note, ne, sizeof(ne));
                    sb_appendf(sb,
                        "<tr>"
                        "<td class='id-col'>%d</td>"
                        "<td class='date-col'>%s</td>"
                        "<td class='date-col'>%s</td>"
                        "<td class='with-col'>%s</td>"
                        "<td class='note-col'>%s</td>"
                        "</tr>",
                        a.id, a.date, a.time, we, ne
                    );
                    found++;
                }
            }
            fclose(f);
            sb_append(sb, "</tbody></table></div>");
        }
        if (found == 0)
            sb_appendf(sb, "<p class='empty' style='margin-top:16px'>No appointments matching &ldquo;%s&rdquo;.</p>", qe);
        else
            sb_appendf(sb, "<p style='margin-top:10px;color:var(--dim);font-size:.72rem;'>%d result(s)</p>", found);
    }

    sb_append(sb, "</div>");
    page_footer(sb);
}

static void build_404(StrBuf *sb) {
    page_header(sb, "");
    sb_append(sb,
        "<div class='panel' style='text-align:center;padding:60px'>"
        "<div style='font-family:Orbitron,sans-serif;font-size:4rem;color:var(--warn);text-shadow:0 0 30px rgba(255,68,68,.5)'>404</div>"
        "<div style='color:var(--dim);margin:16px 0 28px;letter-spacing:.1em;font-size:.8rem'>RESOURCE NOT FOUND</div>"
        "<a href='/' class='btn'>GO HOME</a>"
        "</div>"
    );
    page_footer(sb);
}

/* ─── HTTP request parser ────────────────────────────────────────────────── */

/*
 * Read a full HTTP request from the socket.
 * We only need the first line (method + path + query string).
 * Returns 0 on success, -1 on error/disconnect.
 */
static int read_request(int fd, char *method, size_t msz,
                                char *path,   size_t psz,
                                char *qs,     size_t qsz) {
    char buf[BUFSIZE];
    int  total = 0;
    int  done  = 0;

    method[0] = path[0] = qs[0] = '\0';

    /* Read until we see \r\n\r\n (end of HTTP headers) */
    while (total < (int)sizeof(buf) - 1 && !done) {
        ssize_t n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) return -1;
        total += (int)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n"))
            done = 1;
    }

    /* Parse the request line: METHOD /path?qs HTTP/1.x */
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) line_end = strchr(buf, '\n');
    if (!line_end) return -1;

    char req_line[512];
    size_t req_len = (size_t)(line_end - buf);
    if (req_len >= sizeof(req_line)) req_len = sizeof(req_line) - 1;
    memcpy(req_line, buf, req_len);
    req_line[req_len] = '\0';

    /* tokenise */
    char *sp1 = strchr(req_line, ' ');
    if (!sp1) return -1;
    *sp1 = '\0';
    snprintf(method, msz, "%s", req_line);

    char *url = sp1 + 1;
    char *sp2 = strchr(url, ' ');
    if (sp2) *sp2 = '\0';

    /* split path and query string */
    char *qmark = strchr(url, '?');
    if (qmark) {
        *qmark = '\0';
        snprintf(qs, qsz, "%s", qmark + 1);
    } else {
        qs[0] = '\0';
    }
    snprintf(path, psz, "%s", url);

    /* log the request */
    printf("[%s] %s%s%s\n",
        method, path,
        qs[0] ? "?" : "",
        qs[0] ? qs  : "");
    fflush(stdout);

    return 0;
}

/* ─── Request router ─────────────────────────────────────────────────────── */

static void handle_client(int fd) {
    char method[16], path[256], qs[1024];

    if (read_request(fd, method, sizeof(method),
                         path,   sizeof(path),
                         qs,     sizeof(qs)) < 0) {
        close(fd);
        return;
    }

    /* Only handle GET (forms use GET as required by the practical) */
    if (strcmp(method, "GET") != 0) {
        const char *body = "<h1>405 Method Not Allowed</h1>";
        http_response(fd, "405 Method Not Allowed", "text/html", NULL, body, strlen(body));
        close(fd); return;
    }

    StrBuf sb;
    sb_init(&sb);

    /* ── GET / ── */
    if (strcmp(path, "/") == 0) {
        char ok[128]="", err[128]="";
        get_param(qs, "ok",  ok,  sizeof(ok));
        get_param(qs, "err", err, sizeof(err));
        /* URL-decode the messages */
        char ok2[128], err2[128];
        url_decode(ok, ok2, sizeof(ok2));
        url_decode(err, err2, sizeof(err2));

        build_home(&sb, ok2, err2);
        http_response(fd, "200 OK", "text/html; charset=utf-8", NULL, sb.data, sb.len);

    /* ── GET /add ── */
    } else if (strcmp(path, "/add") == 0) {
        build_add_form(&sb, NULL);
        http_response(fd, "200 OK", "text/html; charset=utf-8", NULL, sb.data, sb.len);

    /* ── GET /do_add?date=...&time=...&with=...&note=... ── */
    } else if (strcmp(path, "/do_add") == 0) {
        char date[16], tm[8], with[64], note[128];
        get_param(qs, "date", date, sizeof(date));
        get_param(qs, "time", tm,   sizeof(tm));
        get_param(qs, "with", with, sizeof(with));
        get_param(qs, "note", note, sizeof(note));

        if (!date[0] || !tm[0] || !with[0]) {
            build_add_form(&sb, "Date, time, and name are required.");
            http_response(fd, "400 Bad Request", "text/html; charset=utf-8", NULL, sb.data, sb.len);
        } else {
            Appointment a;
            memset(&a, 0, sizeof(a));
            a.id = count_appointments() + 1;
            strncpy(a.date, date, sizeof(a.date)-1);
            strncpy(a.time, tm,   sizeof(a.time)-1);
            strncpy(a.with, with, sizeof(a.with)-1);
            strncpy(a.note, note, sizeof(a.note)-1);
            db_add(&a);
            /* Redirect to home with success message — POST/Redirect/Get pattern */
            http_redirect(fd, "/?ok=Appointment+saved.");
        }

    /* ── GET /search?q=... ── */
    } else if (strcmp(path, "/search") == 0) {
        char q[64] = "";
        get_param(qs, "q", q, sizeof(q));
        build_search_form(&sb, q[0] ? q : NULL);
        http_response(fd, "200 OK", "text/html; charset=utf-8", NULL, sb.data, sb.len);

    /* ── GET /delete?id=N ── */
    } else if (strcmp(path, "/delete") == 0) {
        char ids[16];
        get_param(qs, "id", ids, sizeof(ids));
        int id = atoi(ids);
        if (id > 0) {
            db_delete(id);
            http_redirect(fd, "/?ok=Appointment+deleted.");
        } else {
            http_redirect(fd, "/?err=Invalid+ID.");
        }

    /* ── GET /favicon.ico — serve a tiny inline SVG favicon ── */
    } else if (strcmp(path, "/favicon.ico") == 0) {
        /* Return 204 No Content so the browser stops asking */
        const char *hdr =
            "HTTP/1.1 204 No Content\r\n"
            "Connection: close\r\n"
            "\r\n";
        send_all(fd, hdr, strlen(hdr));

    /* ── 404 ── */
    } else {
        build_404(&sb);
        http_response(fd, "404 Not Found", "text/html; charset=utf-8", NULL, sb.data, sb.len);
    }

    free(sb.data);
    close(fd);
    exit(0); /* child exits */
}

/* ─── Main ───────────────────────────────────────────────────────────────── */

/* Reap zombie child processes */
static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void) {
    int sfd, cfd;
    struct sockaddr_in addr;
    int opt = 1;

    signal(SIGCHLD, sigchld_handler);

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(sfd, BACKLOG) < 0) { perror("listen"); return 1; }

    printf("=== HTTP Appointment Server ===\n");
    printf("Listening on http://localhost:%d/\n", PORT);
    printf("Open that URL in your browser.\n\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        cfd = accept(sfd, (struct sockaddr *)&client_addr, &clen);
        if (cfd < 0) continue;

        /* Log client IP */
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("[connect] %s\n", ip);
        fflush(stdout);

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(cfd);
            continue;
        }
        if (pid == 0) {
            /* child */
            close(sfd);
            handle_client(cfd);
            /* handle_client exits internally, but just in case: */
            exit(0);
        }
        /* parent */
        close(cfd);
    }
}