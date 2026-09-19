/* crash_guard.cpp - полноценный pcall на C++: ловит падения процесса,
 * объясняет их и продолжает работу.
 *
 * Как в Lua pcall, только для опасных участков нативного кода: инициализация
 * Vulkan-драйвера и его вызовы кадра идут под ds_guard_run(); если драйвер
 * роняет процесс (SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE), обработчик
 *  1) пишет отчёт: crash_report.txt во внутренней папке + строку с сигналом
 *     и всеми «крошками» запуска в /storage/emulated/0/ds_logs/ds_log.txt
 *     (файл виден игроку без adb),
 *  2) поднимает счётчик vk_fails для Vulkan-сессий,
 *  3) siglongjmp'ом возвращает управление в ds_guard_run - игра продолжает
 *     этот же запуск на CPU-рендере, без перезапуска и чёрного экрана.
 *
 * Почему сигналы, а не try/catch: try/catch в C++ ловит только исключения
 * C++, а драйвер Mali не бросает исключений - он убивает процесс
 * аппаратным SIGSEGV. Единственный способ «поймать» это на Linux/Android -
 * обработчик сигналов + sigsetjmp (кстати, ровно так работает pcall в Lua,
 * только там longjmp через API luaL_error). Модуль собран с
 * -fno-exceptions -fno-rtti: никаких скрытых бросков и зависимости от
 * libc++ - на устройстве важнее предсказуемость, чем синтаксический сахар.
 *
 * Обработчики ставятся конструктором глобального объекта (RAII): до вызова
 * android_main, даже если упадёт самая ранняя инициализация. */

#include "native/crash_guard.h"

#include <csignal>
#include <csetjmp>
#include <cstring>

/* Статические функции .inc переименовываются на включении, чтобы
 * экспортируемые обёртки ниже получили настоящие имена. */
#define ds_crumb ds_crumb_impl
#define ds_crash_report_init ds_crash_report_init_impl
#define ds_crash_report_load ds_crash_report_load_impl
#define ds_crash_report_clear ds_crash_report_clear_impl
#define ds_crash_report_write ds_crash_report_write_impl
#define ds_vk_fail_count ds_vk_fail_count_impl
#define ds_vk_fail_reset ds_vk_fail_reset_impl
#define ds_mem_total_mb ds_mem_total_mb_impl
#define ds_ext_log_init ds_ext_log_init_impl
#define ds_ext_log_set_dir ds_ext_log_set_dir_impl
#define ds_ext_log_write ds_ext_log_write_impl
#include "native/crash_report.inc"
#undef ds_crumb
#undef ds_crash_report_init
#undef ds_crash_report_load
#undef ds_crash_report_clear
#undef ds_crash_report_write
#undef ds_vk_fail_count
#undef ds_vk_fail_reset
#undef ds_mem_total_mb
#undef ds_ext_log_init
#undef ds_ext_log_set_dir
#undef ds_ext_log_write

/* --- внутреннее состояние защиты --- */

static sigjmp_buf ds_guard_env[DS_GUARD_MAX];
static volatile sig_atomic_t ds_guard_depth = 0;
static volatile sig_atomic_t ds_in_crash = 0;
static volatile sig_atomic_t ds_guard_armed = 0;

/* --- обработчик сигналов --- */

extern "C" void ds_guard_signal_handler(int sig, siginfo_t *info, void *uctx) {
    (void)uctx;
    if (ds_in_crash) _exit(70); /* упали внутри собственной записи - выходим */
    ds_in_crash = 1;

    ds_crumb("pcall");
    ds_crash_report_write(sig, info ? info->si_addr : (void *)0);

    if (ds_guard_armed && ds_guard_depth > 0) {
        ds_guard_armed = 0;
        siglongjmp(ds_guard_env[ds_guard_depth - 1], sig ? sig : 1);
    }
    /* Защищённого участка не было: отдаём падение системе - tombstone
     * должен появиться как обычно. */
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(70);
}

namespace {

struct GuardInstaller {
    GuardInstaller() {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = ds_guard_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, (struct sigaction *)0);
        sigaction(SIGBUS, &sa, (struct sigaction *)0);
        sigaction(SIGABRT, &sa, (struct sigaction *)0);
        sigaction(SIGILL, &sa, (struct sigaction *)0);
        sigaction(SIGFPE, &sa, (struct sigaction *)0);
    }
};

/* Ставится при загрузке библиотеки, раньше android_main. */
GuardInstaller ds_guard_installer;

} /* namespace */

/* --- экспортируемые обёртки (статические функции .inc не видны снаружи) --- */

extern "C" void ds_crumb(const char *s) { ds_crumb_impl(s); }

extern "C" void ds_crash_report_init(const char *dir) { ds_crash_report_init_impl(dir); }

extern "C" int ds_crash_report_load(char *out, size_t cap) {
    return ds_crash_report_load_impl(out, cap);
}

extern "C" void ds_crash_report_clear(void) { ds_crash_report_clear_impl(); }

extern "C" void ds_crash_report_write(int sig, void *addr) {
    ds_crash_report_write_impl(sig, addr);
}

extern "C" int ds_vk_fail_count(void) { return ds_vk_fail_count_impl(); }

extern "C" void ds_vk_fail_reset(void) { ds_vk_fail_reset_impl(); }

extern "C" void ds_crash_gpu_mode(int gpu) { ds_vk_fails_enabled = gpu ? 1 : 0; }

extern "C" int ds_mem_total_mb(void) { return ds_mem_total_mb_impl(); }

extern "C" void ds_ext_log_init(void) { ds_ext_log_init_impl(); }

extern "C" void ds_ext_log_set_dir(const char *dir) { ds_ext_log_set_dir_impl(dir); }

extern "C" void ds_ext_log_write(const char *event, int write_crumbs) {
    ds_ext_log_write_impl(event, write_crumbs);
}

extern "C" int ds_ext_log_active(void) { return ds_ext_log_path[0] != 0; }

/* --- сам pcall --- */

extern "C" int ds_guard_run(void (*fn)(void *), void *ctx) {
    if (!fn) return 0;
    if (ds_guard_depth >= DS_GUARD_MAX) {
        /* Глубина исчерпана: выполняем без защиты, честно возвращаем -1. */
        fn(ctx);
        return -1;
    }
    sigjmp_buf *env = &ds_guard_env[ds_guard_depth];
    int jsig = sigsetjmp(*env, 1);
    if (jsig == 0) {
        ds_guard_depth++;
        ds_guard_armed = 1;
        fn(ctx);
        ds_guard_armed = 0;
        ds_guard_depth--;
        return 0;
    }
    ds_guard_depth--;
    return jsig; /* номер пойманного сигнала; отчёт уже записан */
}
