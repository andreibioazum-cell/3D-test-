/* PC preview server: the same C code as the APK, the browser is the window.
 * JPEG transport keeps remote input responsive; PNG assets stay lossless.
 * The app boots into the Minecraft-style main menu; tap "Play" for the
 * Geometrium block world. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "engine.h"
#include "engine/render/rend_internal.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static Buffer frame;
static uint64_t last_ns = 0;
static volatile sig_atomic_t running=1;
const unsigned char *preview_jpeg(const Buffer *frame,size_t *length);
static void shutdown_signal(int signal_number) { (void)signal_number;running=0; }

static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void tick(void) {
    uint64_t now = mono_ns();
    double d = last_ns ? (double)(now - last_ns) / 1e9 : 0;
    last_ns = now;
    if (d < 0) d = 0;
    dt = d;
    game_update();
}

static void render_frame(void) {
    tick();
    if (!gfx_begin_frame(&frame)) return;
    game_draw(&frame);
    gfx_end_frame();
}

static int read_request(int fd, char *buf, int cap, int *body_off, int *body_len) {
    int total = 0;
    int header_end = -1;
    while (total < cap - 1) {
        int n = (int)recv(fd, buf + total, (size_t)(cap - 1 - total), 0);
        if (n <= 0) return -1;
        total += n;
        buf[total] = '\0';
        char *e = strstr(buf, "\r\n\r\n");
        if (e) {
            header_end = (int)(e - buf) + 4;
            break;
        }
    }
    if (header_end < 0) return -1;
    int clen = 0;
    char *cl = strcasestr(buf, "Content-Length:");
    if (cl && cl < buf + header_end) clen = atoi(cl + 15);
    if (clen < 0 || clen > cap - 1 - header_end) return -1;
    while (total < header_end + clen && total < cap - 1) {
        int n = (int)recv(fd, buf + total, (size_t)(header_end + clen - total), 0);
        if (n <= 0) break;
        total += n;
    }
    *body_off = header_end;
    *body_len = total - header_end;
    return total;
}

static void send_all(int fd, const void *data, size_t len) {
    const char *p = (const char *)data;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

static void http_head(int fd, int code, const char *ctype, int len, int cache) {
    char h[256];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                     "Cache-Control: %s\r\nConnection: close\r\n\r\n",
                     code, ctype, len, cache ? "max-age=3600" : "no-store");
    send_all(fd, h, (size_t)n);
}

static void url_decode(char *s) {
    char *o = s;
    while (*s) {
        if (*s == '+') { *o++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else *o++ = *s++;
    }
    *o = '\0';
}

static void form_get(const char *body, int blen, const char *key, char *out, int outsz) {
    out[0] = '\0';
    if (!body || blen <= 0) return;
    char tmp[2048];
    int n = blen < (int)sizeof(tmp) - 1 ? blen : (int)sizeof(tmp) - 1;
    memcpy(tmp, body, (size_t)n);
    tmp[n] = '\0';
    size_t kl = strlen(key);
    char *p = tmp;
    while (p && *p) {
        char *amp = strchr(p, '&');
        if (amp) *amp = '\0';
        char *eq = strchr(p, '=');
        if (eq && (size_t)(eq - p) == kl && strncmp(p, key, kl) == 0) {
            url_decode(eq + 1);
            snprintf(out, (size_t)outsz, "%s", eq + 1);
            return;
        }
        p = amp ? amp + 1 : NULL;
    }
}

static void handle_event(const char *body, int blen) {
    char t[16], k[16], xs[32], ys[32], ids[32];
    form_get(body, blen, "t", t, sizeof(t));
    if (strcmp(t, "down") == 0 || strcmp(t, "move") == 0 || strcmp(t, "up") == 0 || strcmp(t, "cancel-pointer") == 0) {
        form_get(body, blen, "x", xs, sizeof(xs));
        form_get(body, blen, "y", ys, sizeof(ys));
        int action = strcmp(t, "down") == 0 ? 0 : strcmp(t, "up") == 0 ? 1 : strcmp(t, "cancel-pointer") == 0 ? 4 : 2;
        form_get(body, blen, "id", ids, sizeof(ids));
        char *end;
        long id = ids[0] ? strtol(ids, &end, 10) : 0;
        if (id < 0 || id > INT_MAX || (ids[0] && *end)) return;
        game_touch((float)atof(xs), (float)atof(ys), action, (int)id);
    } else if (strcmp(t, "cancel") == 0) {
        game_cancel_input();
        last_ns=0;game_save();
    } else if (strcmp(t, "key") == 0) {
        form_get(body, blen, "k", k, sizeof(k));
        char down_text[8];
        form_get(body, blen, "d", down_text, sizeof(down_text));
        int down = down_text[0] ? atoi(down_text) : 1;
        game_key(k, down);
    }

}

/* The event batch of one request is applied strictly in order. */
static void handle_events(const char *body, int len) {
    while (len > 0) {
        const char *nl = (const char *)memchr(body, '\n', (size_t)len);
        int n = nl ? (int)(nl - body) : len;
        if (n > 0) handle_event(body, n);
        body += n; len -= n;
        if (len > 0) { body++; len--; }
    }
}

static char *read_html(void) {
    FILE *f=fopen("tools/preview/index.html","rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long n=ftell(f);rewind(f);
    if(n<1 || n>65536){fclose(f);return NULL;}
    char *p=malloc((size_t)n+1);
    if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    p[n]=0;fclose(f);return p;
}

int main(int argc, char **argv) {
    int port = 8090;
    int w = 960, h = 540;
    const char *assets = "assets", *storage = "data";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--storage") && i + 1 < argc) storage = argv[++i];
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets = argv[++i];
    }
    if(w<64 || h<64 || w>4096 || h>4096 || port<1 || port>65535) {
        fprintf(stderr,"Invalid preview size or port\n");return 1;
    }
    char *index_html=read_html();
    if(!index_html){fprintf(stderr,"Run preview from the repository root (tools/preview/index.html required)\n");return 1;}
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa={0};sa.sa_handler=shutdown_signal;sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);

    mkdir(storage,0700);
    app_set_storage(storage);
    screen_w = w;
    screen_h = h;
    frame.pixels = (uint32_t *)malloc((size_t)w * h * 4);
    frame.width = w;
    frame.height = h;
    frame.stride = w;
    if (!frame.pixels) { fprintf(stderr, "OOM\n"); return 1; }

    AAssetManager *am = host_asset_manager(assets);
    if (!gfx_init(am)) { fprintf(stderr, "graphics init failed\n"); return 1; }
    game_init(am);
    if (app_failed()) { fprintf(stderr, "%s\n", app_error()); return 1; }
    render_frame();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    listen(srv, 16);
    fprintf(stderr, "Enjoer preview: http://0.0.0.0:%d (%dx%d, assets=%s)\n",
            port, w, h, assets);

    static char reqbuf[16384];
    while (running) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;
        struct timeval timeout = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        int body_off = 0, body_len = 0;
        if (read_request(fd, reqbuf, sizeof(reqbuf), &body_off, &body_len) <= 0) {
            close(fd);
            continue;
        }
        char method[8] = {0}, path[512] = {0};
        sscanf(reqbuf, "%7s %511s", method, path);

        if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
            http_head(fd, 200, "text/html; charset=utf-8", (int)strlen(index_html), 0);
            send_all(fd, index_html, strlen(index_html));
        } else if (strcmp(method, "GET") == 0 && strcmp(path, "/info") == 0) {
            char info[256];
            int n = snprintf(info, sizeof(info),
                             "{\"w\":%d,\"h\":%d,\"fps\":%.1f,\"menu\":%d,\"screen\":\"%s\","
                             "\"volume\":%d,\"quality\":%d}",
                             w, h, rend_fps(), game_menu_open(), game_screen_name(),
                             game_settings_volume(), game_settings_quality());
            http_head(fd, 200, "application/json", n, 0);
            send_all(fd, info, (size_t)n);
        } else if (!strcmp(method,"GET") && !strcmp(path,"/frame.jpg")) {
            render_frame();size_t bytes=0;
            const unsigned char *jpeg=preview_jpeg(&frame,&bytes);
            if(jpeg){http_head(fd,200,"image/jpeg",(int)bytes,0);send_all(fd,jpeg,bytes);}
            else {http_head(fd,500,"text/plain",5,0);send_all(fd,"error",5);}
        } else if (strcmp(method, "GET") == 0 && strcmp(path, "/frame.rgba") == 0) {
            render_frame();
            int bytes = frame.width * frame.height * 4;
            http_head(fd, 200, "application/octet-stream", bytes, 0);
            send_all(fd, frame.pixels, (size_t)bytes);
        } else if (strcmp(method, "POST") == 0 && strcmp(path, "/event") == 0) {
            handle_events(reqbuf + body_off, body_len);
            http_head(fd, 200, "text/plain", 2, 0);
            send_all(fd, "ok", 2);
        } else {
            http_head(fd, 404, "text/plain", 3, 0);
            send_all(fd, "404", 3);
        }
        close(fd);
    }
    game_save();gfx_shutdown();free(frame.pixels);free(index_html);close(srv);
    return 0;
}
