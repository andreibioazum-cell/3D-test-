#include "runtime.h"
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#define DS_ERROR_MESSAGE_SIZE 1024
#define DS_CONSOLE_MAX 256
#define DS_CONSOLE_LINE_MAX 192
static char ds_console_buf[DS_CONSOLE_MAX][DS_CONSOLE_LINE_MAX];
static int ds_console_type_buf[DS_CONSOLE_MAX];
static int ds_console_head = 0;
static int ds_console_count = 0;
static pthread_mutex_t ds_console_lock = PTHREAD_MUTEX_INITIALIZER;
static void console_lock(void) { pthread_mutex_lock(&ds_console_lock); }
static void console_unlock(void) { pthread_mutex_unlock(&ds_console_lock); }
#define DS_CONSOLE_READ_SLOTS 8
static char ds_console_read[DS_CONSOLE_READ_SLOTS][DS_CONSOLE_LINE_MAX];
static int ds_console_read_pos = 0;
static void console_add(const char *line, int is_error) {
    if (!line) return;
    char tmp[DS_CONSOLE_LINE_MAX];
    size_t n = strlen(line);
    size_t w = 0;
    for (size_t i = 0; i < n && w + 1 < sizeof(tmp); i++) {
        char c = line[i];
        tmp[w++] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    tmp[w] = '\0';
    console_lock();
    snprintf(ds_console_buf[ds_console_head], DS_CONSOLE_LINE_MAX, "%s", tmp);
    ds_console_type_buf[ds_console_head] = is_error ? 1 : 0;
    ds_console_head = (ds_console_head + 1) % DS_CONSOLE_MAX;
    if (ds_console_count < DS_CONSOLE_MAX) ds_console_count++;
    console_unlock();
}
int console_count(void) { return ds_console_count; }
int console_type(int index) {
    int t = 0;
    console_lock();
    if (index >= 0 && index < ds_console_count) {
        int pos = (ds_console_head - ds_console_count + index) % DS_CONSOLE_MAX;
        if (pos < 0) pos += DS_CONSOLE_MAX;
        t = ds_console_type_buf[pos];
    }
    console_unlock();
    return t;
}
const char *console_line(int index) {
    console_lock();
    char *slot = ds_console_read[ds_console_read_pos];
    ds_console_read_pos = (ds_console_read_pos + 1) % DS_CONSOLE_READ_SLOTS;
    if (index >= 0 && index < ds_console_count) {
        int pos = (ds_console_head - ds_console_count + index) % DS_CONSOLE_MAX;
        if (pos < 0) pos += DS_CONSOLE_MAX;
        snprintf(slot, DS_CONSOLE_LINE_MAX, "%s", ds_console_buf[pos]);
    } else {
        slot[0] = '\0';
    }
    console_unlock();
    return slot;
}
void console_clear(void) {
    console_lock();
    ds_console_count = 0; ds_console_head = 0;
    console_unlock();
}
Joy joy = {0};
int screen_w = 0;
int screen_h = 0;
double dt = 0.0;
int mouse_clicked = 0;
double ds_mouse_x = 0.0;
double ds_mouse_y = 0.0;
static jmp_buf ds_error_jump;
static int ds_error_handler_active = 0;
static int ds_has_error = 0;
static int ds_restart_requested = 0;
static char ds_last_error[DS_ERROR_MESSAGE_SIZE] = {0};
typedef struct DSStringNode DSStringNode;
struct DSStringNode { DSStringNode *next; char *string; };
static DSStringNode *ds_strings = NULL;
static void platform_log(int is_error, const char *format, va_list args) {
    __android_log_vprint(is_error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "DimScript", format, args);
}
void ds_log(const char *format, ...) {
    char tmp[DS_CONSOLE_LINE_MAX];
    va_list args;
    va_start(args, format);
    platform_log(0, format, args);
    va_end(args);
    va_start(args, format);
    vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    console_add(tmp, 0);
}
void ds_log_err(const char *format, ...) {
    char tmp[DS_CONSOLE_LINE_MAX];
    va_list args;
    va_start(args, format);
    platform_log(1, format, args);
    va_end(args);
    va_start(args, format);
    vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    console_add(tmp, 1);
}
void ds_console_log(int is_error, const char *format, ...) {
    char tmp[DS_CONSOLE_LINE_MAX];
    va_list args;
    va_start(args, format);
    platform_log(is_error, format, args);
    va_end(args);
    va_start(args, format);
    vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    console_add(tmp, is_error ? 1 : 0);
}
void ds_runtime_error(const char *format, ...) {
    char tmp[DS_CONSOLE_LINE_MAX];
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    vsnprintf(ds_last_error, sizeof(ds_last_error), format, copy);
    va_end(copy);
    platform_log(1, format, args);
    va_end(args);
    va_start(args, format);
    vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    console_add(tmp, 1);
    ds_has_error = 1;
    if (ds_error_handler_active) longjmp(ds_error_jump, 1);
}
int ds_call_protected(DSProtectedFunction function, void *userdata, const char *label) {
    int jumped;
    if (!function) {
        if (label && *label) ds_runtime_error("cannot call an empty script hook '%s'", label);
        else ds_runtime_error("cannot call an empty script hook");
        return 0;
    }
    if (ds_error_handler_active) {
        function(userdata);
        return !ds_has_error;
    }
    ds_error_handler_active = 1;
    jumped = setjmp(ds_error_jump);
    if (jumped == 0) {
        function(userdata);
        ds_error_handler_active = 0;
        return !ds_has_error;
    }
    ds_error_handler_active = 0;
    if (label && *label && ds_last_error[0] == '\0') {
        snprintf(ds_last_error, sizeof(ds_last_error), "script hook '%s' failed", label);
    }
    return 0;
}
const char *ds_runtime_error_message(void) { return ds_last_error[0] ? ds_last_error : "unknown DimScript runtime error"; }
int ds_script_has_error(void) { return ds_has_error; }
void ds_clear_runtime_error(void) { ds_has_error = 0; ds_last_error[0] = '\0'; }
void ds_request_script_restart(void) { ds_restart_requested = 1; }
int ds_script_restart_requested(void) { return ds_restart_requested; }
void ds_clear_script_restart(void) { ds_restart_requested = 0; }
static char *ds_strdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s)+1;
    char *c = (char*)malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}
static char *ds_track_string(char *s) {
    if (!s) { ds_runtime_error("out of memory string"); return NULL; }
    DSStringNode *node = (DSStringNode*)malloc(sizeof(*node));
    if (!node) { free(s); ds_runtime_error("out of memory tracking"); return NULL; }
    node->string = s; node->next = ds_strings; ds_strings = node;
    return s;
}
char *ds_num_to_string(double number) {
    char buf[96];
    if (snprintf(buf, sizeof(buf), "%g", number) < 0) return NULL;
    return ds_track_string(ds_strdup(buf));
}
void ds_string_pool_reset(void) {
    DSStringNode *node = ds_strings;
    while (node) { DSStringNode *next = node->next; free(node->string); free(node); node = next; }
    ds_strings = NULL;
}
char *ds_concat(const char *left, const char *right) {
    size_t la = left ? strlen(left) : 0, lb = right ? strlen(right) : 0;
    char *out = (char*)malloc(la+lb+1);
    if (!out) return ds_track_string(ds_strdup(""));
    if (la) memcpy(out, left, la);
    if (lb) memcpy(out+la, right, lb);
    out[la+lb] = '\0';
    return ds_track_string(out);
}
struct DSArray { double *data; size_t len, cap; };
DSArray* arr_new(void) {
    DSArray *a = (DSArray*)calloc(1, sizeof(*a));
    if (!a) { ds_runtime_error("arr_new OOM"); return NULL; }
    a->cap = 8; a->data = (double*)malloc(a->cap*sizeof(double));
    if (!a->data) { free(a); ds_runtime_error("arr_new OOM"); return NULL; }
    return a;
}
void arr_push(DSArray* a, double v) {
    if (!a) return;
    if (a->len >= a->cap) {
        size_t nc = a->cap*2; if (nc<8) nc=8;
        double *nd = (double*)realloc(a->data, nc*sizeof(double));
        if (!nd) { ds_runtime_error("arr_push OOM"); return; }
        a->data = nd; a->cap = nc;
    }
    a->data[a->len++] = v;
}
double arr_get(DSArray* a, double idx) {
    if (!a) return 0;
    long i = (long)idx;
    if (i<0 || (size_t)i>=a->len) return 0;
    return a->data[i];
}
void arr_set(DSArray* a, double idx, double v) {
    if (!a) return;
    long i = (long)idx;
    if (i<0) return;
    if ((size_t)i>=a->len) {
        while (a->len <= (size_t)i) arr_push(a, 0);
    }
    a->data[i] = v;
}
double arr_len(DSArray* a) { return a ? (double)a->len : 0; }
void arr_clear(DSArray* a) { if (a) a->len=0; }
void arr_free(DSArray* a) { if (!a) return; free(a->data); free(a); }
double clamp(double v, double lo, double hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
double lerp(double a, double b, double t){ return a + (b-a)*t; }
double dist(double x1, double y1, double x2, double y2){ double dx=x2-x1, dy=y2-y1; return sqrt(dx*dx+dy*dy); }
/* Математика для скриптов: короткие имена вместо вложенных if'ов. Все они
 * объявлены в runtime.h и перечислены в BUILTINS компилятора (ds_compiler.py)
 * - без записи там скрипт их не увидит. */
double ds_min(double a, double b){ return a<b ? a : b; }
double ds_max(double a, double b){ return a>b ? a : b; }
double ds_abs(double v){ return v<0 ? -v : v; }
double ds_round(double v){ return v<0 ? -floor(-v+0.5) : floor(v+0.5); }
double ds_sign(double v){ return v>0 ? 1 : (v<0 ? -1 : 0); }
/* Остаток от деления и целая часть - для «каждые N» и округления вниз. */
double ds_mod(double a, double b){ if(b==0) return 0; return fmod(a,b); }
double ds_trunc(double v){ return v<0 ? ceil(v) : floor(v); }
double str_len(const char *s){ return s ? (double)strlen(s) : 0; }
int str_eq(const char *a, const char *b){ if(a==b) return 1; if(!a||!b) return 0; return strcmp(a,b)==0; }
int str_contains(const char *hay, const char *needle){
    if(!hay||!needle) return 0;
    if(!*needle) return 1;
    return strstr(hay, needle)!=NULL ? 1 : 0;
}
double str_index_of(const char *hay, const char *needle){
    if(!hay||!needle) return -1;
    const char *p = strstr(hay, needle);
    if(!p) return -1;
    return (double)(p - hay);
}
int str_starts_with(const char *s, const char *pref){
    if(!s||!pref) return 0;
    size_t ls=strlen(s), lp=strlen(pref);
    if(lp>ls) return 0;
    return strncmp(s,pref,lp)==0 ? 1 : 0;
}
int str_ends_with(const char *s, const char *suf){
    if(!s||!suf) return 0;
    size_t ls=strlen(s), lf=strlen(suf);
    if(lf>ls) return 0;
    return strcmp(s+ls-lf,suf)==0 ? 1 : 0;
}
const char *str_sub(const char *s, double start, double len){
    if(!s) return ds_track_string(ds_strdup(""));
    size_t sl=strlen(s);
    long st=(long)start;
    long ln=(long)len;
    if(st<0) st=0;
    if((size_t)st>sl) st=sl;
    if(ln<0) ln=0;
    if((size_t)(st+ln)>sl) ln=sl-st;
    char *out=(char*)malloc((size_t)ln+1);
    if(!out) return ds_track_string(ds_strdup(""));
    memcpy(out,s+st,(size_t)ln);
    out[ln]='\0';
    return ds_track_string(out);
}
double str_to_num(const char *s){
    if(!s) return 0;
    char *end=NULL;
    double v=strtod(s,&end);
    if(end==s) return 0;
    return v;
}
const char *str_trim(const char *s){
    if(!s) return ds_track_string(ds_strdup(""));
    const char *a=s;
    while(*a && (*a==' '||*a=='\t'||*a=='\n'||*a=='\r')) a++;
    const char *b=s+strlen(s);
    while(b>a && (b[-1]==' '||b[-1]=='\t'||b[-1]=='\n'||b[-1]=='\r')) b--;
    size_t ln=b-a;
    char *out=(char*)malloc(ln+1);
    if(!out) return ds_track_string(ds_strdup(""));
    memcpy(out,a,ln);
    out[ln]='\0';
    return ds_track_string(out);
}
const char *str_lower(const char *s){
    if(!s) return ds_track_string(ds_strdup(""));
    size_t l=strlen(s);
    char *out=(char*)malloc(l+1);
    if(!out) return ds_track_string(ds_strdup(""));
    for(size_t i=0;i<l;i++){
        char c=s[i];
        if(c>='A'&&c<='Z') c=c-'A'+'a';
        out[i]=c;
    }
    out[l]='\0';
    return ds_track_string(out);
}
const char *str_upper(const char *s){
    if(!s) return ds_track_string(ds_strdup(""));
    size_t l=strlen(s);
    char *out=(char*)malloc(l+1);
    if(!out) return ds_track_string(ds_strdup(""));
    for(size_t i=0;i<l;i++){
        char c=s[i];
        if(c>='a'&&c<='z') c=c-'a'+'A';
        out[i]=c;
    }
    out[l]='\0';
    return ds_track_string(out);
}