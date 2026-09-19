#ifndef NET_H
#define NET_H
#ifdef __ANDROID__
#include <jni.h>
#endif

#define NET_SLOTS 4
#define NET_OFFLINE 0
#define NET_CONNECTING 1
#define NET_PLAYING 3
#define NET_ERROR 4
#define NET_LOGIN_IDLE 0
#define NET_LOGIN_OK 2
#define NET_LOGIN_BAD_NICK 5
#define NET_LOGIN_WRONG_PASS 6
#define NET_LOGIN_BAD_PASS 7

#ifdef __ANDROID__
void net_set_java_vm(JavaVM *vm);
#endif

void net_connect(const char *url, const char *room);
void net_disconnect(void);
void net_set_data_path(const char *path);
/* Ключ Firebase (API key, ограниченный пакетом com.cb4): включает защищённый
 * режим — все запросы к базе подписываются токеном Firebase Auth. */
void net_set_firebase_key(const char *key);
void net_autologin(const char *url);
double net_auth(const char *url, const char *nick, const char *pass);
double net_set_nick(const char *nick);
void net_logout(void);
double net_login_status(void);
const char *net_login_nick(void);
const char *net_login_pass(void);

void net_publish(double x, double y, double angle, double hp, double alive);
void net_publish_punch(double x, double y, double dx, double dy, double punch);
void net_publish_snow(double x, double y, double dx, double dy, double snow);
/* Турели бука: до трёх живых турелей, каждая со своим положением и HP.
 * count — сколько турелей живо сейчас (0..3). Поля station1_, station2_,
 * station3_ держат турели 1/2/3 по порядку установки. */
void net_publish_turrets(double x1, double y1, double hp1,
                         double x2, double y2, double hp2,
                         double x3, double y3, double hp3,
                         double count);
/* Рывок Азума: стартовая точка, направление и счётчик рывка. Счётчик меняется
 * на каждом рывке — получатели ловят по нему начало рывка. */
void net_publish_dash(double x, double y, double dx, double dy, double dash);
void net_publish_universe(double x, double y, double counter);
/* Контрудар турели бука: счётчик событий «по моей турели ударили». Каждый
 * клиент, увидевший новый счётчик у соперника, сам у себя списывает небольшой
 * урон (каждый авторитетен только над своим бойцом, см. net_player_thud). */
void net_publish_thud(double counter);
void net_set_class(double cls);
double net_status(void);
double net_slot(void);
double net_count(void);
double net_event(void);
void net_event_set(double mode);
double net_player_online(double slot);
double net_player_x(double slot);
double net_player_y(double slot);
double net_player_angle(double slot);
double net_player_hp(double slot);
double net_player_alive(double slot);
const char *net_player_nick(double slot);
double net_player_punch_x(double slot);
double net_player_punch_y(double slot);
double net_player_punch_dx(double slot);
double net_player_punch_dy(double slot);
double net_player_punch(double slot);
double net_player_snow_x(double slot);
double net_player_snow_y(double slot);
double net_player_snow_dx(double slot);
double net_player_snow_dy(double slot);
double net_player_snow(double slot);
double net_player_station_x(double slot);
double net_player_station_y(double slot);
double net_player_station_hp(double slot);
double net_player_station2_x(double slot);
double net_player_station2_y(double slot);
double net_player_station2_hp(double slot);
double net_player_station3_x(double slot);
double net_player_station3_y(double slot);
double net_player_station3_hp(double slot);
/* Сколько турелей живо у игрока (0..3). */
double net_player_station(double slot);
double net_player_universe_x(double slot);
double net_player_universe_y(double slot);
double net_player_universe(double slot);
/* Счётчик контрударов турелей у игрока (0 у старых клиентов — поле просто
 * не присылается, и смешанные версии в онлайне работают). */
double net_player_thud(double slot);
double net_player_dash(double slot);
double net_player_dash_x(double slot);
double net_player_dash_y(double slot);
double net_player_dash_dx(double slot);
double net_player_dash_dy(double slot);
double net_player_class(double slot);
double net_player_level(double slot);
void net_set_level(double level);
/* Скин бойца (0 — обычный, 1 — «Зомби» у Азума): передаётся в снимке комнаты,
 * чтобы его видели соперники. */
void net_set_skin(double skin);
double net_player_skin(double slot);

/* Прогресс без праймов */
double net_load_cups(void);
double net_load_candies(void);
double net_load_class(void);
double net_load_azum(void);
double net_load_santa(void);
double net_load_ebuc(void);
double net_load_level(void);
double net_load_levels_unlocked(void);
double net_load_ordinary_level(void);
double net_load_ordinary_levels_unlocked(void);
double net_load_azum_level(void);
double net_load_azum_levels_unlocked(void);
double net_load_santa_level(void);
double net_load_santa_levels_unlocked(void);
double net_load_ebuc_level(void);
double net_load_ebuc_levels_unlocked(void);
/* Скины Азума. bp_level из батл пасса (убран из игры) остаётся в том же
 * позиционном поле прогресса, чтобы старые сохранения читались как раньше;
 * игра его больше не использует. */
double net_load_bp_level(void);
double net_load_azum_skin(void);
void net_save_progress(double cups, double candies, double cls, double azum, double santa, double ebuc,
                       double level, double levels_unlocked);
void net_save_progress_all(double cups, double candies, double cls, double azum, double santa, double ebuc,
                           double level, double levels_unlocked,
                           double ordinary_level, double ordinary_levels_unlocked,
                           double azum_level, double azum_levels_unlocked,
                           double santa_level, double santa_levels_unlocked,
                           double ebuc_level, double ebuc_levels_unlocked,
                           double bp_level, double azum_skin);

/* Достижения профиля: единый битмаск в achievements.dat. ACH_FLAG_* ниже —
 * код отдельных наград. Добавление новой — это новый бит, старые файлы
 * читаются как есть.
 *  FIRST_WIN      — первая победа;
 *  FIRST_BUY      — первая покупка класса;
 *  ALL_CHARACTERS — собраны все открываемые классы;
 *  LEGENDS        — 25 возрождений за Азума (скин «Зомби»).
 * Бит 0 занимало удалённое достижение «Привет!» — новый код его не
 * выставляет и не читает (старые файлы с ним просто теряют этот бит). */
#define ACH_FLAG_FIRST_WIN       (1u << 1)
#define ACH_FLAG_FIRST_BUY       (1u << 2)
#define ACH_FLAG_ALL_CHARACTERS  (1u << 3)
#define ACH_FLAG_LEGENDS         (1u << 4)
double net_load_achievement_flags(void);
void net_save_achievement_flags(double flags);
double net_has_achievement_flag(double flag);
void net_mark_achievement_flag(double flag);
/* Счётчик возрождений за Азума: хранится вторым числом в achievements.dat,
 * чтобы переживать перезаход и не трогать формат progress.dat. */
double net_load_azum_revives(void);
void net_save_azum_revives(double revives);

/* Персональный промокод: выводится детерминированно из ника (тот же ник —
 * тот же код, у разных игроков — разные коды), поэтому известен даже без
 * сети. net_promo_register пишет код в облачный профиль, net_promo_mark_used
 * помечает его активированным (локально и в облаке) — одна награда на
 * аккаунт. net_promo_streak/bump/reset — счётчик матчей без карточки
 * (растёт шанс выпадения, см. promo.ds). Реализация — native/net/promo.inc. */
const char *net_promo_code(void);
void net_promo_register(void);
double net_promo_used(void);
void net_promo_mark_used(void);
double net_promo_streak(void);
void net_promo_bump_streak(void);
void net_promo_reset_streak(void);
double net_promo_card_found(void);
void net_promo_mark_card_found(void);
double net_load_playtime(void);
void net_save_playtime(double seconds);
void net_add_playtime(double delta);

/* Задания: состояние переживает выход из игры (см. native/net/quests.inc).
 * net_quest_now — текущий epoch, чтобы отсчёт продолжался и после выхода. */
double net_quest_now(void);
void net_save_quest_state(double t0, double p0, double n0, double x0,
                          double t1, double p1, double n1, double x1,
                          double t2, double p2, double n2, double x2);
double net_load_quest_state(double slot, double field);
double net_quest_has_state(void);

void net_leaderboard_fetch(const char *url);
double net_leaderboard_status(void);
double net_leaderboard_count(void);
const char *net_leaderboard_nick(double idx);
double net_leaderboard_cups(double idx);

/* Настройки игрока: язык (0 — English, 1 — русский, как в ui/locale_core.ds),
 * хитбоксы (1 — видны), громкость музыки (0..100, по умолчанию 70),
 * зимняя тема (1 — снежный фон арены и снегопад, по умолчанию) и счётчик FPS
 * в бою (0 — выключен, по умолчанию). Лимита FPS и апскейла в игре больше нет
 * (по просьбе игрока), поэтому и настроек таких не осталось. Живут в
 * settings.dat на устройстве и в профиле /users/<ник> в облаке — то есть
 * сохраняются и там, и там, как прогресс. Реализация — settings_storage.inc. */
double net_load_language(void);
double net_load_hitboxes(void);
void net_save_settings(double language, double hitboxes);
double net_load_music_volume(void);
void net_save_music_volume(double volume);
double net_load_winter_theme(void);
void net_save_winter_theme(double on);
double net_load_fps_meter(void);
void net_save_fps_meter(double on);

/* Согласие с предупреждением об эпилепсии (settings_storage.inc): метка
 * времени нажатия кнопки согласия, хранится только на устройстве. */
void settings_mark_legal(void);
double settings_legal_ts(void);

/* Бан система */
double net_banned(void);
double net_is_banned(const char *nick);
void net_ban_set(const char *nick, double banned);
double net_chat_is_ban(const char *msg);
double net_chat_is_unban(const char *msg);
const char* net_chat_ban_target(const char *msg);
const char* net_chat_unban_target(const char *msg);

/* Команда админа "text <сообщение> <цвет>" — баннер сверху экрана у всех. */
double net_chat_is_text_cmd(const char *msg);
const char* net_chat_text_cmd_text(const char *msg);
const char* net_chat_text_cmd_color(const char *msg);
void net_banner_send(const char *text, const char *color);
double net_banner_ts(void);
const char *net_banner_text(void);
const char *net_banner_color(void);

/* Чат */
void net_chat_send(const char *text);
void net_chat_trim(double keep);
double net_chat_count(void);
const char *net_chat_text(double idx);
const char *net_chat_uid(double idx);
/* Серверный ключ сообщения: уникален на сообщение (uid автора — нет),
 * по нему скрипт понимает, какие сообщения уже показаны пузырями. */
const char *net_chat_key(double idx);

#endif
