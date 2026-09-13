/* Время игры: total_play_time в секундах, хранится локально и в облаке */
#define PLAYTIME_FILE "playtime.dat"

typedef struct {
    DSMutex lock;
    int loaded;
    long long seconds;
} Playtime;
static Playtime pt = { .lock = DS_MUTEX_INIT };

static void pt_lock(void) { ds_mutex_lock(pt.lock); }
static void pt_unlock(void) { ds_mutex_unlock(pt.lock); }

static void playtime_read(void) {
    char path[320];
    FILE *f;
    long long sec = 0;
    pt_lock();
    if (pt.loaded) { pt_unlock(); return; }
    pt_unlock();
    data_file_path(path, sizeof(path), PLAYTIME_FILE);
    f = fopen(path, "r");
    if (f) {
        long long v = 0;
        if (fscanf(f, "%lld", &v) == 1) {
            if (v < 0) v = 0;
            sec = v;
        }
        fclose(f);
    }
    pt_lock();
    pt.seconds = sec;
    pt.loaded = 1;
    pt_unlock();
}

static void playtime_write(long long sec) {
    char path[320], text[64];
    if (sec < 0) sec = 0;
    data_file_path(path, sizeof(path), PLAYTIME_FILE);
    snprintf(text, sizeof(text), "%lld\n", sec);
    file_write_atomic(path, text);
    pt_lock();
    pt.seconds = sec;
    pt.loaded = 1;
    pt_unlock();
}

double net_load_playtime(void) {
    double v;
    playtime_read();
    pt_lock(); v = (double)pt.seconds; pt_unlock();
    return v;
}

void net_save_playtime(double seconds) {
    long long sec = (long long)seconds;
    if (sec < 0) sec = 0;
    playtime_read();
    playtime_write(sec);
    /* В облако */
    char nick[LOGIN_NICK_MAX + 1] = "";
    lg_lock();
    if (lg.status == NET_LOGIN_OK && lg.session_nick[0]) {
        snprintf(nick, sizeof(nick), "%s", lg.session_nick);
    }
    lg_unlock();
    if (!nick[0] || !net.base[0]) return;
    char url[URL], body[BODY];
    snprintf(url, sizeof(url), "%s/users/%s.json", net.base, nick);
    snprintf(body, sizeof(body), "{\"playtime\":%lld}", sec);
    http_patch_async(url, body);
}

void net_add_playtime(double delta) {
    if (delta <= 0) return;
    playtime_read();
    pt_lock();
    long long sec = pt.seconds + (long long)delta;
    pt_unlock();
    net_save_playtime((double)sec);
}

/* Синхронизация с облаком: облако может быть больше локального */
int playtime_sync_with_cloud(const char *resp) {
    long long cloud = (long long)num(resp, "playtime", -1);
    if (cloud < 0) return 0;
    playtime_read();
    pt_lock();
    long long local = pt.seconds;
    if (cloud > local) {
        pt.seconds = cloud;
        pt.loaded = 1;
        pt_unlock();
        playtime_write(cloud);
        return 1;
    }
    pt_unlock();
    return 0;
}
