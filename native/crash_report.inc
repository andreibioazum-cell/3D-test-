/* Служебные писатели отчёта о падении (подключается из crash_guard.cpp).
 *
 * Проблема: на части устройств драйвер падает внутри Vulkan-вызова, а у
 * игрока нет adb - причину не достать. Решение: на каждом шаге запуска
 * остаётся «крошка» (ds_crumb), обработчик (он живёт в crash_guard.cpp)
 * пишет крошки в файл во внутренней папке приложения и во внешний лог
 * /storage/emulated/0/ds_logs/ds_log.txt, а следующий запуск читает его,
 * включает безопасный CPU-режим и показывает отчёт на экране ошибки.
 *
 * Дополнительно файл vk_fails хранит, сколько раз игра упала в Vulkan-сессии:
 * после двух падений следующие запуски стартуют на CPU (драйвер, уронивший
 * процесс дважды, уронит и третий). Чистый выход из Vulkan-сессии обнуляет
 * счётчик - мимолётный сбой не лишает GPU навсегда.
 *
 * Писать можно только async-signal-safe: open/read/write/close и ручное
 * форматирование - никаких snprintf/stdio/locks в обработчике. Крошки
 * складываются без блокировок: вызовы идут только из потока игры.
 *
 * ВАЖНО: на Android 8.x (Go) internalDataPath в android_main бывает NULL -
 * тогда пути уходят на жёсткий фолбэк /data/data/com.cb4 (пакет зафиксирован
 * в манифесте и проверяется CI). Без этого фолбэка отчёт просто не
 * записывается, и следующий запуск не может показать причину. */

#ifndef DS_CRASH_REPORT_INC
#define DS_CRASH_REPORT_INC

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DS_CRUMB_MAX 24
#define DS_CRUMB_LEN 56
#ifndef DS_CRASH_REPORT_MAX
#define DS_CRASH_REPORT_MAX 2048
#endif

/* Внешняя папка логов (видна игроку через любой файловый менеджер).
 * На Android 8..10 нужна запись во внешнее хранилище: разрешение
 * WRITE_EXTERNAL_STORAGE запрашивается из нативного кода, путь доступен
 * после выдачи (со следующего запуска). Если папка недоступна, логи
 * остаются только во внутренней папке приложения. */
#define DS_EXT_LOG_DIR_DEFAULT "/storage/emulated/0/ds_logs"

static char ds_crash_path[256] = {0};
static char ds_vk_fails_path[256] = {0};
static char ds_ext_log_path[256] = {0};
static char ds_crumb_buf[DS_CRUMB_MAX][DS_CRUMB_LEN];
static volatile int ds_crumb_n = 0;
/* 1 = сессия рисует через Vulkan: только её падения поднимают vk_fails
 * (см. ds_crash_gpu_mode в crash_guard.cpp). */
static volatile int ds_vk_fails_enabled = 0;

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

static int ds_open_int_file(const char *path, int flags) {
    return open(path, flags, 0644);
}

/* --- внешний лог (/storage/emulated/0/ds_logs/ds_log.txt) --- */

static void ds_ext_log_set_dir(const char *dir) {
    if (!dir || !*dir) { ds_ext_log_path[0] = 0; return; }
    snprintf(ds_ext_log_path, sizeof(ds_ext_log_path), "%s/ds_log.txt", dir);
}

static void ds_ext_log_init(void) {
    ds_ext_log_set_dir(DS_EXT_LOG_DIR_DEFAULT);
    if (mkdir(DS_EXT_LOG_DIR_DEFAULT, 0755) != 0 && errno != EEXIST) {
        /* Нет разрешения или хранилище не смонтировано - попробуем в следующий
         * запуск (разрешение запрашивает main.c). */
        ds_ext_log_path[0] = 0;
        return;
    }
    int fd = ds_open_int_file(ds_ext_log_path, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) { ds_ext_log_path[0] = 0; return; }
    close(fd);
}

/* Добавляет в ds_log.txt строку события и текущие крошки. Доступ к внешнему
 * хранилищу безопасен и в обработчике (open/write/close AS-safe). */
static void ds_ext_log_write(const char *event, int write_crumbs) {
    if (!ds_ext_log_path[0]) return;
    int fd = ds_open_int_file(ds_ext_log_path, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) return;
    ds_w_str(fd, "[");
    ds_w_int(fd, (long)time(NULL));
    ds_w_str(fd, "] ");
    ds_w_str(fd, event);
    ds_w_str(fd, "\n");
    if (write_crumbs) {
        int total = ds_crumb_n;
        if (total > DS_CRUMB_MAX) total = DS_CRUMB_MAX;
        for (int i = 0; i < total; i++) {
            ds_w_str(fd, "  - ");
            ds_w_str(fd, ds_crumb_buf[(ds_crumb_n - total + i) % DS_CRUMB_MAX]);
            ds_w_str(fd, "\n");
        }
    }
    close(fd);
}

/* Сама запись отчёта: crash_report.txt во внутренней папке + строка «crash
 * signal N» во внешний лог. Зовётся из обработчика crash_guard.cpp. */
static void ds_crash_report_write(int sig, void *addr) {
    if (ds_crash_path[0]) {
        int fd = ds_open_int_file(ds_crash_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            ds_w_str(fd, "CubicBattle4 crash: signal ");
            ds_w_int(fd, sig);
            ds_w_str(fd, " addr 0x");
            ds_w_hex(fd, (unsigned long)(uintptr_t)addr);
            ds_w_str(fd, ds_vk_fails_enabled ? " (vulkan)\n" : " (cpu)\n");
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
    }
    {
        char head[96];
        char *h = head;
        const char *p = "crash signal ";
        /* Ручная склейка вместо snprintf: функция зовётся из обработчика. */
        while (*p) *h++ = *p++;
        if (sig >= 10) *h++ = (char)('0' + sig / 10);
        *h++ = (char)('0' + sig % 10);
        *h = 0;
        ds_ext_log_write(head, 1);
    }
    /* Падение в Vulkan-сессии: поднимаем счётчик, чтобы следующие запуски
     * включили CPU сразу, не дожидаясь повторных убытков. */
    if (ds_vk_fails_enabled && ds_vk_fails_path[0]) {
        char b = '0';
        int fd2 = ds_open_int_file(ds_vk_fails_path, O_RDONLY);
        if (fd2 >= 0) {
            if (read(fd2, &b, 1) != 1) b = '0';
            close(fd2);
        }
        if (b < '0' || b > '8') b = '0';
        fd2 = ds_open_int_file(ds_vk_fails_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd2 >= 0) {
            char nb = (char)(b + 1);
            write(fd2, &nb, 1);
            close(fd2);
        }
    }
}

static void ds_crash_report_init(const char *dir) {
    if (!dir || !*dir) dir = "/data/data/com.cb4"; /* см. ВАЖНО выше */
    snprintf(ds_crash_path, sizeof(ds_crash_path), "%s/crash_report.txt", dir);
    snprintf(ds_vk_fails_path, sizeof(ds_vk_fails_path), "%s/vk_fails", dir);
}

/* Отчёт прошлого запуска: 1 - есть (в out лежит текст). */
static int ds_crash_report_load(char *out, size_t cap) {
    if (!out || cap < 2) return 0;
    out[0] = '\0';
    if (!ds_crash_path[0]) return 0;
    int fd = ds_open_int_file(ds_crash_path, O_RDONLY);
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

/* Сколько раз уже падали в Vulkan-сессиях (по файлу vk_fails). */
static int ds_vk_fail_count(void) {
    if (!ds_vk_fails_path[0]) return 0;
    int fd = ds_open_int_file(ds_vk_fails_path, O_RDONLY);
    if (fd < 0) return 0;
    char b = '0';
    if (read(fd, &b, 1) != 1) b = '0';
    close(fd);
    if (b < '0' || b > '9') return 0;
    return b - '0';
}

/* Чистая Vulkan-сессия: снова можно пробовать Vulkan. */
static void ds_vk_fail_reset(void) {
    if (!ds_vk_fails_path[0]) return;
    int fd = ds_open_int_file(ds_vk_fails_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
}

/* Объём RAM в МБ по /proc/meminfo (или -1, если не смогли прочитать). */
static int ds_mem_total_mb(void) {
    int fd = ds_open_int_file("/proc/meminfo", O_RDONLY);
    if (fd < 0) return -1;
    char buf[512];
    size_t got = 0;
    while (got + 1 < sizeof(buf)) {
        ssize_t r = read(fd, buf + got, sizeof(buf) - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    buf[got] = '\0';
    if (strncmp(buf, "MemTotal:", 9) != 0) return -1;
    const char *p = buf + 9;
    while (*p == ' ' || *p == '\t') p++;
    int v = 0, any = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; }
    if (!any) return -1;
    return (v + 1023) / 1024; /* kB -> MB с округлением вверх */
}

#endif /* DS_CRASH_REPORT_INC */
