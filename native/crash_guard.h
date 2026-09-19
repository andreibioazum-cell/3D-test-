/* Публичный API модуля приёма падений и pcall (реализация - crash_guard.cpp).
 * Подключается из main.c и хост-тестов; сам модуль собирается как C++. */

#ifndef DS_CRASH_GUARD_H
#define DS_CRASH_GUARD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Сколько падений Vulkan прощаем; дальше следующие запуски стартуют на CPU. */
#define DS_VK_MAX_FAILS 2

/* Максимальная глубина вложенных pcall (init внутри кадра и т.п.). */
#define DS_GUARD_MAX 4

/* Буфер под текст отчёта о падении. */
#define DS_CRASH_REPORT_MAX 2048

/* Крошка этапа запуска: «boot», «gfx-vk-init», «f240» (240-й кадр) и т.п. */
void ds_crumb(const char *s);

/* Инициализация путей (internalDataPath; NULL -> /data/data/com.cb4) и
 * установка обработчиков SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE. */
void ds_crash_report_init(const char *dir);

/* Отчёт прошлого запуска: 1 - есть (текст в out). */
int ds_crash_report_load(char *out, size_t cap);

/* Игрок закрыл экран отчёта - стираем файл, следующий запуск снова GPU. */
void ds_crash_report_clear(void);

/* Прямая запись отчёта о падении (тесты; реальный сценарий идёт через
 * обработчик сигнала). Пишет crash_report.txt + строку в ds_log.txt и
 * поднимает vk_fails. */
void ds_crash_report_write(int sig, void *addr);

/* Счётчик падений Vulkan-сессий и сброс после чистой сессии. */
int ds_vk_fail_count(void);
void ds_vk_fail_reset(void);

/* 1 - текущая сессия рисует через Vulkan: её падение поднимет vk_fails. */
void ds_crash_gpu_mode(int gpu);

/* Объём RAM в МБ по /proc/meminfo (или -1). */
int ds_mem_total_mb(void);

/* Внешний лог /storage/emulated/0/ds_logs/ds_log.txt. */
void ds_ext_log_init(void);
void ds_ext_log_set_dir(const char *dir);
void ds_ext_log_write(const char *event, int write_crumbs);
int ds_ext_log_active(void);

/* pcall: выполнить fn(ctx) под защитой обработчиков. Возвращает 0, если
 * fn отработал нормально; номер сигнала, если драйвер/код уронил процесс
 * внутри fn (в этом случае отчёт уже записан и в ds_log.txt, и во
 * внутреннюю папку); -1, если превышена глубина вложенности. */
int ds_guard_run(void (*fn)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DS_CRASH_GUARD_H */
