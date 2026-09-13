/* Топ по времени игры */
typedef struct {
    char nick[24];
    long long playtime;
} TopTimeEntry;
typedef struct {
    DSMutex lock;
    int status;
    int count;
    TopTimeEntry entries[16];
} TopTime;
static TopTime tt = { .lock = DS_MUTEX_INIT };

static int parse_toptime(const char *json, TopTimeEntry *out, int max_out) {
    const char *p = skip_ws(json);
    int count = 0;
    if (!p || *p != '{') return 0;
    for (p = skip_ws(p + 1); p && *p && *p != '}' && count < 64; ) {
        const char *kend = skip_str(p);
        if (!kend) break;
        char key[32] = {0};
        size_t kl = (size_t)(kend - p - 2);
        if (kl >= sizeof(key)) kl = sizeof(key) - 1;
        memcpy(key, p + 1, kl); key[kl] = 0;
        p = skip_ws(kend);
        if (*p != ':') break;
        p = skip_ws(p + 1);
        const char *obj = p;
        p = skip_val(p);
        if (!p) break;
        if (*obj == '{') {
            char nick[24] = {0};
            strv(obj, "nick", nick, sizeof(nick));
            if (!nick[0]) snprintf(nick, sizeof(nick), "%s", key);
            long long pt = (long long)num(obj, "playtime", 0);
            if (pt < 0) pt = 0;
            snprintf(out[count].nick, sizeof(out[count].nick), "%s", nick);
            out[count].playtime = pt;
            count++;
        }
        p = skip_ws(p);
        if (p && *p == ',') p = skip_ws(p + 1);
    }
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (out[j].playtime < out[j + 1].playtime) {
                TopTimeEntry tmp = out[j];
                out[j] = out[j + 1];
                out[j + 1] = tmp;
            }
        }
    }
    if (count > max_out) count = max_out;
    return count;
}
static void *tt_fetch_job(void *arg) {
    char *base = (char *)arg;
    char url[URL], resp[65536];
    snprintf(url, sizeof(url), "%s/users.json", base);
    free(base);
    int code = fb_http("GET", url, NULL, resp, sizeof(resp));
    if (code != 200) {
        ds_mutex_lock(tt.lock);
        tt.status = 4;
        ds_mutex_unlock(tt.lock);
        return NULL;
    }
    TopTimeEntry tmp[64];
    int cnt = parse_toptime(resp, tmp, 10);
    ds_mutex_lock(tt.lock);
    tt.count = cnt;
    for (int i = 0; i < cnt; i++) tt.entries[i] = tmp[i];
    tt.status = 2;
    ds_mutex_unlock(tt.lock);
    return NULL;
}
void net_toptime_fetch(const char *url) {
    const char *base = (url && *url) ? url : net.base;
    if (!base || !*base) return;
    ds_mutex_lock(tt.lock);
    tt.status = 1;
    tt.count = 0;
    ds_mutex_unlock(tt.lock);
    char *arg = (char*)malloc(URL);
    if (!arg) return;
    snprintf(arg, URL, "%s", base);
    DSThread t;
    t = ds_thread_start(tt_fetch_job, arg);
    if (t) ds_thread_detach(t);
    else {
        ds_mutex_lock(tt.lock); tt.status = 4; ds_mutex_unlock(tt.lock);
        free(arg);
    }
}
double net_toptime_status(void) {
    double s;
    ds_mutex_lock(tt.lock); s = (double)tt.status; ds_mutex_unlock(tt.lock);
    return s;
}
double net_toptime_count(void) {
    double c;
    ds_mutex_lock(tt.lock); c = (double)tt.count; ds_mutex_unlock(tt.lock);
    return c;
}
static char s_tt_nick_ret[24];
const char *net_toptime_nick(double idx) {
    int i = (int)idx;
    ds_mutex_lock(tt.lock);
    if (i >= 0 && i < tt.count) snprintf(s_tt_nick_ret, sizeof(s_tt_nick_ret), "%s", tt.entries[i].nick);
    else s_tt_nick_ret[0] = 0;
    ds_mutex_unlock(tt.lock);
    return s_tt_nick_ret;
}
double net_toptime_playtime(double idx) {
    int i = (int)idx;
    double v = 0;
    ds_mutex_lock(tt.lock);
    if (i >= 0 && i < tt.count) v = (double)tt.entries[i].playtime;
    ds_mutex_unlock(tt.lock);
    return v;
}
