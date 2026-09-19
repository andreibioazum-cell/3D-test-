/* Регрессионный тест арены строк скрипта (native/runtime/core.inc).
 *
 * Версия с ареной, слипшаяся из PR «Бой: единые прозрачные хитбоксы...»,
 * падала при первой же неудачной аллокации: ds_arena_alloc звал
 * ds_runtime_error (это longjmp - хук скрипта «умирает»), а ds_concat и
 * ds_num_to_string возвращали NULL вместо строки. На слабом телефоне одна
 * отказавшая аллокация в init() зацикливала перезапуск скрипта навсегда
 * («захожу в приложение - вылет»), а NULL в строковой переменной рано или
 * поздно добирался до strlen(NULL). До арены контракт был другим: malloc
 * вернул NULL - строковая функция молча отдавала "" и игра продолжалась.
 *
 * Тест подменяет malloc через -Wl,--wrap=malloc и проверяет ровно это:
 *   1. отказали 8 МБ -> арена ужимается до меньшего размера, строки работают;
 *   2. отказали ВСЕ аллокации -> строки из статического резерва, ds_concat /
 *      ds_num_to_string / str_sub / str_trim / str_lower / str_upper никогда
 *      не возвращают NULL, хук не прерывается, ds_has_error не выставляется;
 *   3. str_sub с абсурдной длиной (1e18) не роняет хук даже без кучи;
 *   4. сброс арены (ds_string_pool_reset) продолжает работать в бою.
 *
 * Сборка и запуск (из корня репозитория):
 *   gcc -std=gnu99 -O1 -o /tmp/test_string_arena \
 *       tools/host_test/test_string_arena.c runtime.c \
 *       -I tools/host_test/stub -I . -lm \
 *       -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
 *   /tmp/test_string_arena
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

/* --- подмена malloc: как настоящий, но по приказу теста возвращает NULL --- */
static int g_fail_malloc = 0;   /* 0 - всё разрешено; 1 - только большие; 2 - все */
static long g_allocated = 0;

extern void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
    if (g_fail_malloc == 2) return NULL;
    if (g_fail_malloc == 1 && size >= 1024u * 1024u) return NULL;
    void *p = __real_malloc(size);
    if (p) g_allocated += (long)size;
    return p;
}
extern void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size) {
    if (g_fail_malloc == 2) return NULL;
    void *p = __real_calloc(n, size);
    if (p) g_allocated += (long)(n * size);
    return p;
}
extern void *__real_realloc(void *ptr, size_t size);
void *__wrap_realloc(void *ptr, size_t size) {
    if (g_fail_malloc == 2) return NULL;
    return __real_realloc(ptr, size);
}
extern void __real_free(void *p);
void __wrap_free(void *p) { __real_free(p); }

/* Заглушка платформенного лога (на устройстве это liblog). */
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag; (void)fmt; return 0;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    (void)prio; (void)tag; (void)fmt; (void)ap; return 0;
}

/* --- настоящий рантайм (арена внутри) --- */
#include "runtime.c"

/* Хук, который делает то же, что и скрипты: тысячи склеек строк. */
static int g_hook_reached_end = 0;
static void heavy_string_hook(void *userdata) {
    (void)userdata;
    char *acc = NULL;
    for (int i = 0; i < 2000; i++) {
        acc = ds_concat(acc ? acc : "x", ds_num_to_string(i));
        if (!acc) { fprintf(stderr, "FAIL: ds_concat вернул NULL в хуке\n"); return; }
        if (acc[strlen(acc)] != '\0') { fprintf(stderr, "FAIL: строка без терминатора\n"); return; }
    }
    if (!str_sub("привет", 0, 1e18)) { fprintf(stderr, "FAIL: str_sub вернул NULL\n"); return; }
    if (!str_trim("  x  ") || !str_lower("ABC") || !str_upper("abc")) {
        fprintf(stderr, "FAIL: str_trim/lower/upper вернули NULL\n");
        return;
    }
    g_hook_reached_end = 1;
}

int main(void) {
    int fail = 0;

    /* 1. Отказ только больших аллокаций: арена 8 МБ не выделилась,
     *    игра обязана жить на меньшей. */
    g_fail_malloc = 1;
    {
        int ok = ds_call_protected(heavy_string_hook, NULL, "init");
        if (!ok) { printf("FAIL: хук прерван при отказе 8 МБ (longjmp из арены)\n"); fail = 1; }
        if (!g_hook_reached_end) { printf("FAIL: хук не дошёл до конца при отказе 8 МБ\n"); fail = 1; }
    }
    g_fail_malloc = 0;

    /* Сброс: арена уже выделена, хук снова работает. */
    ds_string_pool_reset();
    g_hook_reached_end = 0;
    if (!ds_call_protected(heavy_string_hook, NULL, "update") || !g_hook_reached_end) {
        printf("FAIL: повторный хук после сброса арены\n"); fail = 1;
    }

    /* 2. Отказали ВСЕ аллокации: строки из статического резерва,
     *    ни одна функция не возвращает NULL и не роняет хук. */
    g_fail_malloc = 2;
    ds_string_pool_reset();
    g_hook_reached_end = 0;
    {
        int ok = ds_call_protected(heavy_string_hook, NULL, "init");
        if (!ok) { printf("FAIL: хук прерван при полном отказе кучи\n"); fail = 1; }
        if (!g_hook_reached_end) { printf("FAIL: хук не дошёл до конца при полном отказе кучи\n"); fail = 1; }
        /* Напрямую: NULL недопустим ни из одной строковой функции. */
        if (!ds_concat("a", "b")) { printf("FAIL: ds_concat == NULL без кучи\n"); fail = 1; }
        if (!ds_num_to_string(42)) { printf("FAIL: ds_num_to_string == NULL без кучи\n"); fail = 1; }
        if (!str_sub("abc", 1, 2)) { printf("FAIL: str_sub == NULL без кучи\n"); fail = 1; }
        if (!str_trim(" x ")) { printf("FAIL: str_trim == NULL без кучи\n"); fail = 1; }
        if (!str_lower("X") || !str_upper("x")) { printf("FAIL: str_lower/upper == NULL без кучи\n"); fail = 1; }
        /* 3. Абсурдная длина без кучи - не падение и не прерывание. */
        if (!str_sub("abc", 0, 1e18)) { printf("FAIL: str_sub(1e18) == NULL без кучи\n"); fail = 1; }
        if (ds_script_has_error()) { printf("FAIL: арена выставила ds_has_error\n"); fail = 1; }
    }
    g_fail_malloc = 0;

    /* 4. После снятия отказа всё продолжает работать. */
    ds_string_pool_reset();
    {
        char *s = ds_concat("Cubic", "Battle");
        if (!s || strcmp(s, "CubicBattle") != 0) { printf("FAIL: ds_concat сломан после восстановления\n"); fail = 1; }
    }

    if (!fail) printf("PASS: аллокация строк не роняет хук и не возвращает NULL (отказ 8 МБ, полный отказ кучи, длина 1e18)\n");
    return fail;
}
