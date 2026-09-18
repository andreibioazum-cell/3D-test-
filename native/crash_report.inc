/* Отчёт о падении: последний крик умирающего процесса.
 *
 * Проблема: на части устройств драйвер падает внутри Vulkan-вызова (tombstone
 * «null pointer dereference» в libGLES_mali), а у игрока нет adb - причину
 * не достать. Решение: на каждом шаге запуска остаётся «крошка» (ds_crumb),
 * обработчик SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE пишет крошки в файл во
 * внутренней папке приложения, а следующий запуск читает его, включает
 * безопасный CPU-режим и показывает отчёт на экране ошибки - игрок просто
 * присылает скриншот.
 *
 * Писать можно только async-signal-safe: open/write/close и ручное
 * форматирование - никаких snprintf/stdio/locks. Крошки складываются без
 * блокировок: вызовы идут только из потока игры. */

#ifndef DS_CRASH_REPORT_INC
#define DS_CRASH_REPORT_INC

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define DS_CRUMB_MAX 24
#define DS_CRUMB_LEN 56
#define DS_CRASH_REPORT_MAX 2048

static char ds_crumb_buf[DS_CRUMB_MAX][DS_CRUMB_LEN];
static volatile int ds_crumb_n = 0;
static char ds_crash_path[256] = {0};
static volatile sig_atomic_t ds_in_crash = 0;

/* Метка этапа запуска: «boot», «gfx-vk-init», «f240» (240-й кадр) и т.п. */
static void ds_crumb(const char *s) {
    if (!s) return;
    char *dst = ds_crumb_buf[ds_crumb_n % DS_CRUMB_MAX];
    size_t i = 0;
    for (; s[i] && i + 1 < DS_CRUMB_LEN; i++) dst[i] = s[i];
    dst[i] = '\0';
    ds_crumb_n++;
}

static void ds_w_str(int fd, const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (n) write(fd, s, n);
}
static void ds_w_int(int fd, long v) {
    char tmp[24];
    int i = (int)(sizeof(tmp) - 1);
    unsigned long u = v < 0 ? (unsigned long)-(v + 1) + 1ul : (unsigned long)v;
    tmp[i--] = '\0';
    if (!u) tmp[i--] = '0';
    while (u) { tmp[i--] = (char)('0' + (u % 10)); u /= 10; }
    if (v < 0) tmp[i--] = '-';
    write(fd, tmp + i + 1, sizeof(tmp) - 2 - (size_t)i);
}
static void ds_w_hex(int fd, unsigned long v) {
    static const char digits[] = "0123456789abcdef";
    char tmp[20];
    int i = (int)(sizeof(tmp) - 1);
    tmp[i--] = '\0';
    if (!v) tmp[i--] = '0';
    while (v) { tmp[i--] = digits[v & 0xf]; v >>= 4; }
    tmp[i--] = 'x';
    tmp[i--] = '0';
    write(fd, tmp + i + 1, sizeof(tmp) - 2 - (size_t)i);
}

/* Сама запись отчёта. Вынесена из обработчика, чтобы хост-тест мог вызвать
 * её без реального сигнала (ASan перехватывает SIGSEGV раньше нас). */
static void ds_crash_report_write(int sig, void *addr) {
    if (!ds_crash_path[0]) return;
    int fd = open(ds_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    ds_w_str(fd, "CubicBattle4 crash: signal ");
    ds_w_int(fd, sig);
    ds_w_str(fd, " addr 0x");
    ds_w_hex(fd, (unsigned long)(uintptr_t)addr);
    ds_w_str(fd, "\n");
    int total = ds_crumb_n;
    if (total > DS_CRUMB_MAX) total = DS_CRUMB_MAX;
    for (int i = 0; i < total; i++) {
        const char *s = ds_crumb_buf[(ds_crumb_n - total + i) % DS_CRUMB_MAX];
        ds_w_str(fd, "- ");
        ds_w_str(fd, s);
        ds_w_str(fd, "\n");
    }
    close(fd);
}

static void ds_crash_handler(int sig, siginfo_t *info, void *uctx) {
    (void)uctx;
    if (ds_in_crash) _exit(70);
    ds_in_crash = 1;
    ds_crash_report_write(sig, info ? info->si_addr : NULL);
    /* Отдаём падение системе: tombstone должен появиться как обычно. */
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(70);
}

static void ds_crash_report_init(const char *dir) {
    if (dir && *dir) snprintf(ds_crash_path, sizeof(ds_crash_path), "%s/crash_report.txt", dir);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ds_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

/* Отчёт прошлого запуска: 1 - есть (в out лежит текст). */
static int ds_crash_report_load(char *out, size_t cap) {
    if (!out || cap < 2) return 0;
    out[0] = '\0';
    if (!ds_crash_path[0]) return 0;
    int fd = open(ds_crash_path, O_RDONLY);
    if (fd < 0) return 0;
    size_t got = 0;
    while (got + 1 < cap) {
        ssize_t r = read(fd, out + got, cap - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    out[got] = '\0';
    return got > 0;
}

static void ds_crash_report_clear(void) {
    if (ds_crash_path[0]) unlink(ds_crash_path);
}

#endif /* DS_CRASH_REPORT_INC */
