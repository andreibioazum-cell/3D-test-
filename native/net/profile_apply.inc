/* Профиль игрока из облака: разбор записи /users/<ник> и слияние с устройством.
 *
 * Вынесено из native/net/auth_session.inc. Здесь три вещи:
 *   apply_user_json            - разобрать JSON профиля в локальный прогресс;
 *   push_pg_to_cloud           - отправить локальный прогресс в облако;
 *   apply_user_json_keep_local - то же слияние, но облако может только
 *                                добавлять: купленное на устройстве не теряется.
 * Используют pg/progress_write из state_storage.inc, num/strv из auth_json.inc,
 * settings_sync_with_cloud из settings_storage.inc и promo_sync_with_cloud из
 * promo.inc - все они подключены раньше в net.c.
 */
/* Разбор записи /users/<ник> в локальный прогресс (без пароля: в защищённом
 * режиме пароль проверяет Firebase Auth, а не база). */
static void apply_user_json(const char *resp) {
    int cups = (int)num(resp, "cups", 0);
    int candies = (int)num(resp, "candies", 0);
    int cls = (int)num(resp, "cls", 0);
    int azum = (int)num(resp, "azum", 0);
    int santa = (int)num(resp, "santa", 0);
    int ebuc = (int)num(resp, "ebuc", 0);
    int level = (int)num(resp, "level", 0);
    int levels_unlocked = (int)num(resp, "levels", -1);
    int ordinary_level = (int)num(resp, "ordinary_level", -1);
    int ordinary_levels_unlocked = (int)num(resp, "ordinary_levels", -1);
    int azum_level = (int)num(resp, "azum_level", -1);
    int azum_levels_unlocked = (int)num(resp, "azum_levels", -1);
    int santa_level = (int)num(resp, "santa_level", -1);
    int santa_levels_unlocked = (int)num(resp, "santa_levels", -1);
    int ebuc_level = (int)num(resp, "ebuc_level", -1);
    int ebuc_levels_unlocked = (int)num(resp, "ebuc_levels", -1);
    int bp_level = (int)num(resp, "bp", 0);
    int azum_skin = (int)num(resp, "azum_skin", 0);
    if (levels_unlocked < 0) levels_unlocked = level;
    if (cups < 0) cups = 0;
    if (candies < 0) candies = 0;
    if (cls != 1 && cls != 2 && cls != 3) cls = 0;
    azum = azum ? 1 : 0;
    santa = santa ? 1 : 0;
    ebuc = ebuc ? 1 : 0;
    if (cls == 1 && !azum) cls = 0;
    if (cls == 2 && !santa) cls = 0;
    if (cls == 3 && !ebuc) cls = 0;
    if (ordinary_level < 0 && ordinary_levels_unlocked < 0 &&
        azum_level < 0 && azum_levels_unlocked < 0 &&
        santa_level < 0 && santa_levels_unlocked < 0 &&
        ebuc_level < 0 && ebuc_levels_unlocked < 0) {
        ordinary_level=ordinary_levels_unlocked=0;
        azum_level=azum_levels_unlocked=0;
        santa_level=santa_levels_unlocked=0;
        ebuc_level=ebuc_levels_unlocked=0;
        if (cls==1 && azum) { azum_level=level; azum_levels_unlocked=levels_unlocked; }
        else if (cls==2 && santa) { santa_level=level; santa_levels_unlocked=levels_unlocked; }
        else if (cls==3 && ebuc) { ebuc_level=level; ebuc_levels_unlocked=levels_unlocked; }
        else { ordinary_level=level; ordinary_levels_unlocked=levels_unlocked; }
    } else {
        if (ordinary_level < 0) ordinary_level = 0;
        if (ordinary_levels_unlocked < 0) ordinary_levels_unlocked = ordinary_level;
        if (azum_level < 0) azum_level = 0;
        if (azum_levels_unlocked < 0) azum_levels_unlocked = azum_level;
        if (santa_level < 0) santa_level = 0;
        if (santa_levels_unlocked < 0) santa_levels_unlocked = santa_level;
        if (ebuc_level < 0) ebuc_level = 0;
        if (ebuc_levels_unlocked < 0) ebuc_levels_unlocked = ebuc_level;
    }
    normalize_level_pair(&level, &levels_unlocked);
    normalize_level_pair(&ordinary_level, &ordinary_levels_unlocked);
    normalize_level_pair(&azum_level, &azum_levels_unlocked);
    normalize_level_pair(&santa_level, &santa_levels_unlocked);
    normalize_level_pair(&ebuc_level, &ebuc_levels_unlocked);
    if (cls == 0) { level=ordinary_level; levels_unlocked=ordinary_levels_unlocked; }
    else if (cls == 1) { level=azum_level; levels_unlocked=azum_levels_unlocked; }
    else if (cls == 2) { level=santa_level; levels_unlocked=santa_levels_unlocked; }
    else { level=ebuc_level; levels_unlocked=ebuc_levels_unlocked; }
    if (bp_level < 0) bp_level = 0;
    if (bp_level > BP_MAX) bp_level = BP_MAX;
    azum_skin = azum_skin ? 1 : 0;
    pg_lock();
    pg.cups = cups; pg.candies = candies; pg.cls = cls; pg.azum = azum; pg.santa = santa; pg.ebuc = ebuc;
    pg.level = level; pg.levels_unlocked = levels_unlocked;
    pg.ordinary_level = ordinary_level; pg.ordinary_levels_unlocked = ordinary_levels_unlocked;
    pg.azum_level = azum_level; pg.azum_levels_unlocked = azum_levels_unlocked;
    pg.santa_level = santa_level; pg.santa_levels_unlocked = santa_levels_unlocked;
    pg.ebuc_level = ebuc_level; pg.ebuc_levels_unlocked = ebuc_levels_unlocked;
    pg.bp_level = bp_level; pg.azum_skin = azum_skin;
    pg.loaded = 1;
    pg_unlock();
    pending_cls = cls;
    pending_level = level;
    pending_skin = azum_skin;
    progress_write(cups, candies, cls, azum, santa, ebuc, level, levels_unlocked,
                   ordinary_level, ordinary_levels_unlocked,
                   azum_level, azum_levels_unlocked,
                   santa_level, santa_levels_unlocked,
                   ebuc_level, ebuc_levels_unlocked,
                   bp_level, azum_skin);
    /* Достижения из облака: если в записи пользователя есть ach_flags,
     * загружаем их в локальный файл (облако - источник правды). */
    int cloud_ach_flags = (int)num(resp, "ach_flags", -1);
    int cloud_ach_revives = (int)num(resp, "ach_revives", -1);
    if (cloud_ach_flags >= 0) {
        ds_mutex_lock(ach_lock);
        /* Облачные достижения перетирают локальные только если они «новее»
         * (больше битов установлено) или если локальных ещё нет. */
        unsigned int local_f = ach_flags;
        unsigned int local_r = ach_revives;
        int loaded_local = ach_loaded;
        ds_mutex_unlock(ach_lock);
        unsigned int cf = (unsigned int)cloud_ach_flags;
        unsigned int cr = cloud_ach_revives >= 0 ? (unsigned int)cloud_ach_revives : 0;
        int cloud_is_newer = !loaded_local || (cf | local_f) != local_f || cr > local_r;
        if (cloud_is_newer) {
            ds_mutex_lock(ach_lock);
            ach_flags = cf | ach_flags; /* объединяем биты: ничего не теряется */
            if (cr > ach_revives) ach_revives = cr;
            unsigned int wf = ach_flags, wr = ach_revives;
            ach_loaded = 1;
            ds_mutex_unlock(ach_lock);
            achievements_write(wf, wr);
            LOG("cloud achievements merged: flags=%u revives=%u", wf, wr);
        }
    }
    /* Настройки из той же записи: см. settings_sync_with_cloud. */
    settings_sync_with_cloud(resp);
    /* Промокод из той же записи: см. promo_sync_with_cloud в promo.inc. */
    promo_sync_with_cloud(resp);
}

/* Протолкнуть текущий локальный прогресс в облако (только после того, как
 * сессия логина уже установлена - иначе PATCH уйдёт без авторизации). */
static void push_pg_to_cloud(void) {
    int cups, candies, cls, azum, santa, ebuc, lv, lu;
    int ol, ou, al, au, sl, su, el, eul, bp, sk;
    pg_lock();
    cups = pg.cups; candies = pg.candies; cls = pg.cls;
    azum = pg.azum; santa = pg.santa; ebuc = pg.ebuc;
    lv = pg.level; lu = pg.levels_unlocked;
    ol = pg.ordinary_level; ou = pg.ordinary_levels_unlocked;
    al = pg.azum_level; au = pg.azum_levels_unlocked;
    sl = pg.santa_level; su = pg.santa_levels_unlocked;
    el = pg.ebuc_level; eul = pg.ebuc_levels_unlocked;
    bp = pg.bp_level; sk = pg.azum_skin;
    pg_unlock();
    net_save_progress_all(cups, candies, cls, azum, santa, ebuc,
                          lv, lu, ol, ou, al, au, sl, su, el, eul, bp, sk);
}

/* Облачная запись может отставать от локального устройства: записи в облако
 * асинхронные (http_patch_async), и покупка могла потеряться (сбой сети,
 * старые правила, не пускавшие поля ebuc / ebuc_level / ebuc_levels и cls=3).
 * После перезахода такая облачная запись «стирала» купленный локально класс.
 * Теперь возвращаем локальное состояние для каждого купленного класса
 * (Азум/Дед Мороз/бук + уровни) и выбранный класс: облако может добавлять,
 * но никогда не отнимает то, что уже есть на устройстве. Возврат 1 - облако
 * нужно обновить после логина (push_pg_to_cloud в net_auth). */
static int apply_user_json_keep_local(const char *resp) {
    int had = 0, restored = 0, dirty = cloud_dirty_get();
    int local_azum = 0, local_santa = 0, local_ebuc = 0, local_cls = 0;
    int local_al = 0, local_au = 0, local_sl = 0, local_su = 0, local_el = 0, local_eu = 0;
    int local_cups = 0, local_candies = 0, local_ol = 0, local_ou = 0, local_sk = 0;
    progress_read();
    pg_lock();
    if (pg.loaded) {
        had = 1;
        local_azum = pg.azum; local_santa = pg.santa; local_ebuc = pg.ebuc;
        local_cls = pg.cls;
        local_al = pg.azum_level; local_au = pg.azum_levels_unlocked;
        local_sl = pg.santa_level; local_su = pg.santa_levels_unlocked;
        local_el = pg.ebuc_level; local_eu = pg.ebuc_levels_unlocked;
        local_cups = pg.cups; local_candies = pg.candies;
        local_ol = pg.ordinary_level; local_ou = pg.ordinary_levels_unlocked;
        local_sk = pg.azum_skin;
    }
    pg_unlock();
    apply_user_json(resp);
    if (!had) return 0;
    int cloud_azum, cloud_santa, cloud_ebuc;
    pg_lock();
    cloud_azum = pg.azum; cloud_santa = pg.santa; cloud_ebuc = pg.ebuc;
    /* Последняя запись с этого устройства до облака не дошла: локальный файл
     * новее облачной записи, поэтому валюты, уровни обычного и скин тоже
     * берём с устройства (иначе купленное офлайн «оплатится» дважды - кубки
     * вернутся из облака, а класс останется). */
    if (dirty) {
        if (pg.cups != local_cups) { pg.cups = local_cups; restored = 1; }
        if (pg.candies != local_candies) { pg.candies = local_candies; restored = 1; }
        if (local_ou > pg.ordinary_levels_unlocked) { pg.ordinary_levels_unlocked = local_ou; restored = 1; }
        if (local_ol > pg.ordinary_level) { pg.ordinary_level = local_ol; restored = 1; }
        if (pg.azum_skin != local_sk) { pg.azum_skin = local_sk; restored = 1; }
    }
    if (local_azum == 1 && pg.azum == 0) { pg.azum = 1; restored = 1; }
    if (local_santa == 1 && pg.santa == 0) { pg.santa = 1; restored = 1; }
    if (local_ebuc == 1 && pg.ebuc == 0) { pg.ebuc = 1; restored = 1; }
    if (local_al > pg.azum_level) { pg.azum_level = local_al; restored = 1; }
    if (local_au > pg.azum_levels_unlocked) { pg.azum_levels_unlocked = local_au; restored = 1; }
    if (local_sl > pg.santa_level) { pg.santa_level = local_sl; restored = 1; }
    if (local_su > pg.santa_levels_unlocked) { pg.santa_levels_unlocked = local_su; restored = 1; }
    if (local_el > pg.ebuc_level) { pg.ebuc_level = local_el; restored = 1; }
    if (local_eu > pg.ebuc_levels_unlocked) { pg.ebuc_levels_unlocked = local_eu; restored = 1; }
    /* Выбранный класс возвращаем только если облако «забыло» именно его
     * (куплен локально, в облаке нет): тогда cloud cls уже сломан
     * (apply_user_json свёл его на обычный), и выбор игрока теряется.
     * Если облако знает класс - его выбор (cls) свежее локального файла. */
    if (local_cls > 0 && local_cls < 4) {
        int local_owned = 0, was_in_cloud = 0;
        if (local_cls == 1) { local_owned = local_azum; was_in_cloud = cloud_azum; }
        else if (local_cls == 2) { local_owned = local_santa; was_in_cloud = cloud_santa; }
        else { local_owned = local_ebuc; was_in_cloud = cloud_ebuc; }
        if (local_owned && !was_in_cloud && pg.cls != local_cls) { pg.cls = local_cls; restored = 1; }
        /* При «грязном» устройстве локальный выбор тоже свежее облачного. */
        else if (dirty && local_owned && pg.cls != local_cls) { pg.cls = local_cls; restored = 1; }
    } else if (dirty && local_cls == 0 && pg.cls != 0) {
        pg.cls = 0; restored = 1;
    }
    /* Зеркало «уровень выбранного класса» пересобираем под финальный cls. */
    if (pg.cls == 0) { pg.level = pg.ordinary_level; pg.levels_unlocked = pg.ordinary_levels_unlocked; }
    else if (pg.cls == 1) { pg.level = pg.azum_level; pg.levels_unlocked = pg.azum_levels_unlocked; }
    else if (pg.cls == 2) { pg.level = pg.santa_level; pg.levels_unlocked = pg.santa_levels_unlocked; }
    else { pg.level = pg.ebuc_level; pg.levels_unlocked = pg.ebuc_levels_unlocked; }
    int cups = pg.cups, candies = pg.candies, cls = pg.cls;
    int azum = pg.azum, santa = pg.santa, ebuc = pg.ebuc, lv = pg.level, lu = pg.levels_unlocked;
    int ol = pg.ordinary_level, ou = pg.ordinary_levels_unlocked;
    int al = pg.azum_level, au = pg.azum_levels_unlocked;
    int sl = pg.santa_level, su = pg.santa_levels_unlocked;
    int el = pg.ebuc_level, eul = pg.ebuc_levels_unlocked;
    int bp = pg.bp_level, sk = pg.azum_skin;
    pg_unlock();
    if (restored) {
        pending_cls = cls; pending_level = lv; pending_skin = sk;
        progress_write(cups, candies, cls, azum, santa, ebuc, lv, lu,
                       ol, ou, al, au, sl, su, el, eul, bp, sk);
        LOG("restored locally bought progress over cloud (azum=%d santa=%d ebuc=%d cls=%d)",
            azum, santa, ebuc, cls);
        return 1;
    }
    /* Устройство помнит, что последняя запись в облако не дошла (progress.dirty):
     * даже если по классам расхождений нет, кубки/леденцы/уровни могли
     * отстать - досылаем весь локальный прогресс. */
    if (cloud_dirty_get()) {
        LOG("cloud profile marked stale on device - pushing local progress");
        return 1;
    }
    return 0;
}

