#include "net.h"
#include "runtime.h"
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#define LOG(...) do { __android_log_print(ANDROID_LOG_INFO, "DimScriptNet", __VA_ARGS__); ds_console_log(0, __VA_ARGS__); } while (0)
#define LOGERR(...) do { __android_log_print(ANDROID_LOG_ERROR, "DimScriptNet", __VA_ARGS__); ds_console_log(1, __VA_ARGS__); } while (0)
#define URL 512
#define BODY 1024
#define RESP 4096
#define CHAT_RESP 8192
#define WRITE_TICK 60
#define READ_TICK 60
#define CHAT_TICK 1000
#define EVENT_TICK 500
/* Баннер админа ("text ..."): опрос banner.json раз в секунду - команда не
 * претендует на мгновенную доставку, как позиция игрока. */
#define BANNER_TICK 1000
#define BANNER_COLOR_MAX 16
/* Анти-дудос со своей стороны: если состояние игрока не меняется, слот всё
 * равно «тикать» должен чаще, чем TIMEOUT других клиентов. */
#define HEARTBEAT_TICK 2500
/* Чистку старых сообщений чата не нужно гонять каждую секунду. */
#define CHAT_PRUNE_TICK 5000
#define TIMEOUT 4000
#define STALE 6000
#define CHAT_MAX 32
#define CHAT_TEXT_MAX 96
#define LOGIN_NICK_MAX 16
#define SESSION_FILE "auth.dat"
#define PROGRESS_FILE "progress.dat"
#define ACHIEVEMENTS_FILE "achievements.dat"
#define BANS_FILE "bans.dat"
#define BAN_MAX 64
#define BAN_TICK 2000
#define BAN_WAIT 2500
#define BAN_KICK_TICK 2000

/* Firebase Auth: буферы под подписанные токеном URL (URL + ?auth=<idToken>). */
#define FB_URL_MAX 2048
#define FB_ID_MAX 1100
#define FB_REFRESH_MAX 512
typedef pthread_mutex_t DSMutex;
#define DS_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define ds_mutex_lock(m) pthread_mutex_lock(&(m))
#define ds_mutex_unlock(m) pthread_mutex_unlock(&(m))
typedef pthread_t DSThread;
static DSThread ds_thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t;
    if (pthread_create(&t, NULL, fn, arg) != 0) return (pthread_t)0;
    return t;
}
static void ds_thread_join(DSThread t) { if (t) pthread_join(t, NULL); }
static void ds_thread_detach(DSThread t) { if (t) pthread_detach(t); }

typedef struct {
    double x,y,a,hp,alive;
    double punch_x,punch_y,punch_dx,punch_dy,punch;
    double snow_x,snow_y,snow_dx,snow_dy,snow;
    double station_x,station_y,station_hp,station;
    double station2_x,station2_y,station2_hp;
    double station3_x,station3_y,station3_hp;
    double dash_x,dash_y,dash_dx,dash_dy,dash;
    double universe_x,universe_y,universe;
    double thud;
    double cls, level, skin;
    int online;
    char nick[24];
} Actor;
typedef struct { char key[28]; char uid[24]; char nick[24]; char text[CHAT_TEXT_MAX]; int valid; } ChatMsg;
static int chat_keep = 20;
static struct {
    DSThread thread, rthread; DSMutex lock;
    JavaVM *vm;
    int run, started, slot, status;
    int self_banned;
    char base[256], room[48], uid[24];
    Actor me; unsigned long seq, count;
    Actor players[NET_SLOTS];
    ChatMsg chats[CHAT_MAX]; int chat_count;
    int event;
    char banner_text[CHAT_TEXT_MAX];
    char banner_color[BANNER_COLOR_MAX];
    long long banner_ts;
} net = { .lock = DS_MUTEX_INIT };

/* Список банов, прочитанный из облака. Это общий для всех клиентов источник
 * правды: локальный bans.dat есть только у того, кто ставил бан, поэтому
 * выкидывать из комнаты чужого забаненного игрока можно лишь по этому списку. */
typedef struct {
    DSMutex lock;
    char nick[BAN_MAX][LOGIN_NICK_MAX + 1];
    int count, synced;
    long long synced_at;
} BanList;
static BanList banlist = { .lock = DS_MUTEX_INIT };
static void ban_lock(void) { ds_mutex_lock(banlist.lock); }
static void ban_unlock(void) { ds_mutex_unlock(banlist.lock); }

static volatile sig_atomic_t net_fast = 0;
/* Статики наблюдения за слотами (claim_slot) и приёма игроков (read_players):
 * живут на уровне файла, чтобы net_connect мог сбросить их при каждом новом
 * подключении - иначе записи прошлой сессии «помнят» чужие uid/seq и старые
 * метки времени, что путает захват слота и определение онлайна после
 * переподключения. */
static unsigned long claim_seen_seq[NET_SLOTS];
static long long claim_seen_at[NET_SLOTS];
static char claim_seen_uid[NET_SLOTS][24];
static unsigned long rp_lseq[NET_SLOTS];
static long long rp_lch[NET_SLOTS];
static long long rp_kicked[NET_SLOTS];
static char s_chat_text_ret[CHAT_TEXT_MAX];
static char s_chat_uid_ret[24];
static char s_nick_ret[24];
static char s_lg_nick_ret[24];
static char s_lg_pass_ret[64];
static char s_ban_target_ret[32];
static char s_banner_text_ret[CHAT_TEXT_MAX];
static char s_banner_color_ret[BANNER_COLOR_MAX];
static char s_text_cmd_text_ret[CHAT_TEXT_MAX];
static char s_text_cmd_color_ret[BANNER_COLOR_MAX];
static long long now_ms(void);
static void lock(void);
static void unlock(void);

typedef struct {
    DSMutex lock;
    int status;
    char session_nick[LOGIN_NICK_MAX+1];
    char session_pass[64];
    char path[256];
} Login;
static Login lg = { .lock = DS_MUTEX_INIT };
static void lg_lock(void) { ds_mutex_lock(lg.lock); }
static void lg_unlock(void) { ds_mutex_unlock(lg.lock); }

typedef struct {
    DSMutex lock;
    int loaded, cups, candies, cls, azum, santa, ebuc, level, levels_unlocked;
    int ordinary_level, ordinary_levels_unlocked;
    int azum_level, azum_levels_unlocked;
    int santa_level, santa_levels_unlocked;
    int ebuc_level, ebuc_levels_unlocked;
    int bp_level, azum_skin;
} Progress;
static Progress pg = { .lock = DS_MUTEX_INIT };
#define LEVEL_MAX 3
#define BP_MAX 5
#define SKIN_MAX 1
static int pending_cls = 0;
static int pending_level = 0;
/* Скин своего бойца: как и класс с уровнем, уходит в сетевой снимок, чтобы
 * соперники видели Азума-зомби, а не только сам игрок. */
static int pending_skin = 0;
static void pg_lock(void) { ds_mutex_lock(pg.lock); }
static void pg_unlock(void) { ds_mutex_unlock(pg.lock); }
static void data_file_path(char *path, size_t cap, const char *name) {
    lg_lock();
    if (lg.path[0]) snprintf(path, cap, "%s/%s", lg.path, name);
    else snprintf(path, cap, "%s", name);
    lg_unlock();
}
static int clamp_level_value(int value) {
    if (value < 0) return 0;
    if (value > LEVEL_MAX) return LEVEL_MAX;
    return value;
}
static int clamp_levels_unlocked(int value) {
    if (value < 0) return 0;
    if (value > LEVEL_MAX) return LEVEL_MAX;
    return value;
}
static void normalize_level_pair(int *level, int *unlocked) {
    *unlocked = clamp_levels_unlocked(*unlocked);
    *level = clamp_level_value(*level);
    if (*level > *unlocked) *level = 0;
}
/* Атомарная запись: временный файл + rename. Убийство посреди записи не
 * обнуляет прогресс/достижения обрывком файла - старый файл остаётся цел. */
static void file_write_atomic(const char *path, const char *text) {
    char tmp[400]; FILE *f;
    size_t n = strlen(text);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (f && (n == 0 || fwrite(text, 1, n, f) == n)) {
        fclose(f);
        rename(tmp, path);
        return;
    }
    if (f) fclose(f);
    remove(tmp);
}

/* Файл прогресса (progress.dat): запись, чтение и все геттеры net_load_*
 * вынесены в native/net/progress_file.inc - вместе с разбором старых
 * форматов файла. Объявлены здесь, потому что сохранение прогресса
 * (net_save_progress_all ниже) и вход по нику (auth_session.inc) вызывают их
 * раньше, чем они определены. */
static void progress_write(int cups, int candies, int cls, int azum, int santa, int ebuc,
                           int level, int levels_unlocked,
                           int ordinary_level, int ordinary_levels_unlocked,
                           int azum_level, int azum_levels_unlocked,
                           int santa_level, int santa_levels_unlocked,
                           int ebuc_level, int ebuc_levels_unlocked,
                           int bp_level, int azum_skin);
static void progress_read(void);


typedef struct { char plain[URL]; char url[FB_URL_MAX]; char body[BODY*2]; int progress; } HttpJob;
static int fb_enabled(void);
static void fb_url(char *dst, size_t cap, const char *url);
static int fb_ensure_token(void);
static int fb_refresh_token_now(void);
static int net_log_ok(void);
static int http(const char *method,const char *url,const char *body,char *out,size_t cap);
static void *http_delete_job(void *arg);
static void room_slot_delete_async(int slot);
static void sleep_ms(int ms);
/* progress.dirty = «облако отстало от устройства»: PATCH профиля не прошёл
 * (нет сети, сервер лёг). Следующий вход дошлёт локальный прогресс целиком. */
#define PROGRESS_DIRTY_FILE "progress.dirty"
static void cloud_dirty_set(int on) {
    char path[320];
    data_file_path(path, sizeof(path), PROGRESS_DIRTY_FILE);
    if (on) file_write_atomic(path, "1\n");
    else remove(path);
}
static int cloud_dirty_get(void) {
    char path[320]; FILE *f;
    data_file_path(path, sizeof(path), PROGRESS_DIRTY_FILE);
    f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}
/* Облачные записи (прогресс, достижения) уходят PATCH'ем из фоновой нити.
 * Надёжность важна: раньше токен перед записью не обновлялся, и через час
 * после входа idToken протухал - PATCH уходил со старым токеном, Firebase
 * отвечал 401, а ответ нигде не проверялся. Покупка так и не доходила до
 * облака, и после перезахода (особенно с другого телефона) класс «исчезал».
 * Теперь: токен обновляем до записи, при 401 - форсируем refresh и
 * повторяем один раз. */
static int http_patch_once(HttpJob *j) {
    int code;
    if (fb_enabled()) {
        fb_ensure_token();
        fb_url(j->url, sizeof(j->url), j->plain);
        code = http("PATCH", j->url, j->body, NULL, 0);
        if (code == 401 && fb_refresh_token_now()) {
            fb_url(j->url, sizeof(j->url), j->plain);
            code = http("PATCH", j->url, j->body, NULL, 0);
        }
    } else {
        code = http("PATCH", j->url, j->body, NULL, 0);
    }
    return code;
}
/* Повторы облачной записи: 0 (нет ответа), 5xx и 429 - временные, шлём снова
 * через 1, 2, 4 с; 400/403 - правила базы, повтор бессмысленен. */
#define PATCH_RETRIES 4
/* Порядковый номер записи прогресса в облако (0 - не прогресс). */
static DSMutex patch_seq_lock = DS_MUTEX_INIT;
static int patch_seq = 0;
static void *http_patch_job(void *arg) {
    HttpJob *j = (HttpJob*)arg;
    if (!j) return NULL;
    int code = 0, attempt = 0, wait = 1000;
    for (;;) {
        code = http_patch_once(j);
        if (code == 200) break;
        int transient = (code == 0 || code == 429 || code >= 500);
        if (!transient || ++attempt >= PATCH_RETRIES) break;
        sleep_ms(wait);
        if (wait < 4000) wait *= 2;
    }
    if (code != 200) {
        if (j->progress) cloud_dirty_set(1);
        if (net_log_ok()) LOGERR("cloud PATCH failed: HTTP %d%s", code,
                                 j->progress ? " (progress will be resent on next login)" : "");
    } else {
        /* Метку снимает только самая свежая запись прогресса: если старый
         * PATCH дошёл позже нового, а новый упал, устройство всё ещё «грязное». */
        if (j->progress) {
            int latest;
            ds_mutex_lock(patch_seq_lock);
            latest = (j->progress == patch_seq);
            ds_mutex_unlock(patch_seq_lock);
            if (latest) cloud_dirty_set(0);
        }
        if (attempt > 0) LOG("cloud PATCH ok after %d retry(ies)", attempt);
    }
    free(j);
    return NULL;
}
static void http_patch_async_ex(const char *url, const char *body, int progress) {
    HttpJob *j = (HttpJob*)malloc(sizeof(*j));
    DSThread t;
    if (!j) return;
    snprintf(j->plain, sizeof(j->plain), "%s", url);
    snprintf(j->url, sizeof(j->url), "%s", url);
    snprintf(j->body, sizeof(j->body), "%s", body);
    j->progress = 0;
    if (progress) {
        ds_mutex_lock(patch_seq_lock);
        j->progress = ++patch_seq;
        ds_mutex_unlock(patch_seq_lock);
    }
    t = ds_thread_start(http_patch_job, j);
    if (t) { ds_thread_detach(t); return; }
    if (progress) cloud_dirty_set(1);
    free(j);
}
static void http_patch_async(const char *url, const char *body) {
    http_patch_async_ex(url, body, 0);
}

void net_save_progress(double cups, double candies, double cls, double azum, double santa, double ebuc,
                       double level, double levels_unlocked) {
    int k=(int)cls;
    int ol,ou,al,au,sl,su,el,eu,bp,sk;
    progress_read();
    pg_lock();
    ol=pg.ordinary_level; ou=pg.ordinary_levels_unlocked;
    al=pg.azum_level; au=pg.azum_levels_unlocked;
    sl=pg.santa_level; su=pg.santa_levels_unlocked;
    el=pg.ebuc_level; eu=pg.ebuc_levels_unlocked;
    bp=pg.bp_level; sk=pg.azum_skin;
    pg_unlock();
    if (k==0) { ol=(int)level; ou=(int)levels_unlocked; }
    else if (k==1) { al=(int)level; au=(int)levels_unlocked; }
    else if (k==2) { sl=(int)level; su=(int)levels_unlocked; }
    else if (k==3) { el=(int)level; eu=(int)levels_unlocked; }
    net_save_progress_all(cups,candies,cls,azum,santa,ebuc,level,levels_unlocked,
                          ol,ou,al,au,sl,su,el,eu,bp,sk);
}

/* Переменные достижений (используются и здесь для сохранения в Firebase, и
 * в auth_json.inc для чтения/записи локального файла). */
static DSMutex ach_lock = DS_MUTEX_INIT;
static int ach_loaded = 0;
static unsigned int ach_flags = 0;
static unsigned int ach_revives = 0;

void net_save_progress_all(double cups, double candies, double cls, double azum, double santa, double ebuc,
                           double level, double levels_unlocked,
                           double ordinary_level, double ordinary_levels_unlocked,
                           double azum_level, double azum_levels_unlocked,
                           double santa_level, double santa_levels_unlocked,
                           double ebuc_level, double ebuc_levels_unlocked,
                           double bp_level, double azum_skin) {
    int c = (int)cups, cd = (int)candies, k = (int)cls, a = azum ? 1 : 0, sn = santa ? 1 : 0, eu = ebuc ? 1 : 0;
    int lv = (int)level, lu = (int)levels_unlocked;
    int ol = (int)ordinary_level, ou = (int)ordinary_levels_unlocked;
    int al = (int)azum_level, au = (int)azum_levels_unlocked;
    int sl = (int)santa_level, su = (int)santa_levels_unlocked;
    int el = (int)ebuc_level, eul = (int)ebuc_levels_unlocked;
    int bp = (int)bp_level, sk = azum_skin ? 1 : 0;
    if (c < 0) c = 0;
    if (cd < 0) cd = 0;
    if (k != 1 && k != 2 && k != 3) k = 0;
    // Владение персонажами - липкое: раз куплен, не теряется даже если
    // в облаке старая запись без него (фикс бага «буК пропал после перезахода»).
    // Файл читаем обязательно: первое сохранение после запуска раньше шло
    // мимо него (pg ещё не загружен) и затирало купленное на устройстве.
    progress_read();
    pg_lock();
    if (pg.loaded) {
        if (pg.azum == 1) a = 1;
        if (pg.santa == 1) sn = 1;
        if (pg.ebuc == 1) eu = 1;
        // Уровни тоже только растут: облако не может откатить открытый уровень.
        if (pg.ordinary_levels_unlocked > ou) ou = pg.ordinary_levels_unlocked;
        if (pg.azum_levels_unlocked > au) au = pg.azum_levels_unlocked;
        if (pg.santa_levels_unlocked > su) su = pg.santa_levels_unlocked;
        if (pg.ebuc_levels_unlocked > eul) eul = pg.ebuc_levels_unlocked;
        if (pg.ordinary_level > ol) ol = pg.ordinary_level;
        if (pg.azum_level > al) al = pg.azum_level;
        if (pg.santa_level > sl) sl = pg.santa_level;
        if (pg.ebuc_level > el) el = pg.ebuc_level;
    }
    pg_unlock();
    if (k == 1 && !a) k = 0;
    if (k == 2 && !sn) k = 0;
    if (k == 3 && !eu) k = 0;
    if (bp < 0) bp = 0;
    if (bp > BP_MAX) bp = BP_MAX;
    normalize_level_pair(&lv, &lu);
    normalize_level_pair(&ol, &ou);
    normalize_level_pair(&al, &au);
    normalize_level_pair(&sl, &su);
    normalize_level_pair(&el, &eul);
    if (k == 0) { lv=ol; lu=ou; }
    else if (k == 1) { lv=al; lu=au; }
    else if (k == 2) { lv=sl; lu=su; }
    else { lv=el; lu=eul; }
    pg_lock();
    pg.cups = c; pg.candies = cd; pg.cls = k; pg.azum = a; pg.santa = sn; pg.ebuc = eu;
    pg.level = lv; pg.levels_unlocked = lu; pg.loaded = 1;
    pg.ordinary_level = ol; pg.ordinary_levels_unlocked = ou;
    pg.azum_level = al; pg.azum_levels_unlocked = au;
    pg.santa_level = sl; pg.santa_levels_unlocked = su;
    pg.ebuc_level = el; pg.ebuc_levels_unlocked = eul;
    pg.bp_level = bp; pg.azum_skin = sk;
    pg_unlock();
    pending_cls = k;
    pending_level = lv;
    pending_skin = sk;
    progress_write(c, cd, k, a, sn, eu, lv, lu, ol, ou, al, au, sl, su, el, eul, bp, sk);

    char nick[LOGIN_NICK_MAX + 1] = "";
    lg_lock();
    if (lg.status == NET_LOGIN_OK && lg.session_nick[0]) {
        snprintf(nick, sizeof(nick), "%s", lg.session_nick);
    }
    lg_unlock();
    if (nick[0] && net.base[0]) {
        char url[URL], body[BODY*3];
        /* Достижения тоже уходят в облако: каждый раз при сохранении прогресса
         * битмаск наград и счётчик возрождений пишутся в профиль игрока. */
        unsigned int af = 0, ar = 0;
        ds_mutex_lock(ach_lock);
        af = ach_flags; ar = ach_revives;
        ds_mutex_unlock(ach_lock);
        snprintf(url, sizeof(url), "%s/users/%s.json", net.base, nick);
        snprintf(body, sizeof(body),
                 "{\"cups\":%d,\"candies\":%d,\"cls\":%d,\"azum\":%d,\"santa\":%d,\"ebuc\":%d,\"level\":%d,\"levels\":%d,\"ordinary_level\":%d,\"ordinary_levels\":%d,\"azum_level\":%d,\"azum_levels\":%d,\"santa_level\":%d,\"santa_levels\":%d,\"ebuc_level\":%d,\"ebuc_levels\":%d,\"bp\":%d,\"azum_skin\":%d,\"ach_flags\":%u,\"ach_revives\":%u}",
                 c, cd, k, a, sn, eu, lv, lu, ol, ou, al, au, sl, su, el, eul, bp, sk, af, ar);
        /* Пока запись в пути, профиль считается несинхронизированным: если
         * приложение убьют раньше ответа, следующий вход дошлёт прогресс. */
        cloud_dirty_set(1);
        http_patch_async_ex(url, body, 1);
    } else {
        /* Без сессии (играли офлайн) покупка живёт только на устройстве -
         * помечаем, чтобы при первом же входе она ушла в облако. */
        cloud_dirty_set(1);
    }
}
/* Достижения - отдельный маленький файл с единым битмаском: ACH_FLAG_* задают
 * отдельные награды. Добавление нового бита не ломает старые записи. Путь
 * хранится рядом с аккаунтом и прогрессом. Реализация функций - в
 * auth_json.inc. */