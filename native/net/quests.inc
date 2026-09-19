/* Состояние заданий переживает выход из игры: 3 слота × {тип, прогресс,
 * норма, метка появления следующего}. Метка — реальное время (epoch), поэтому
 * отсчёт «5 минут» продолжается, даже если игра закрыта. */
#define QUESTS_FILE "quests.dat"
#define QUEST_SLOTS 3
#define QUEST_FIELDS 4

static double quest_state[QUEST_SLOTS * QUEST_FIELDS] = {0};
static int quest_state_loaded = 0;
static int quest_state_valid = 0;

double net_quest_now(void) { return (double)time(NULL); }

static void quest_state_read(void) {
    char path[320]; FILE *f;
    if (quest_state_loaded) return;
    data_file_path(path, sizeof(path), QUESTS_FILE);
    f = fopen(path, "r");
    if (f) {
        double v[QUEST_SLOTS * QUEST_FIELDS];
        int i, got = 0;
        for (i = 0; i < QUEST_SLOTS * QUEST_FIELDS; i++) {
            if (fscanf(f, "%lf", &v[i]) != 1) break;
            got++;
        }
        if (got == QUEST_SLOTS * QUEST_FIELDS) {
            for (i = 0; i < QUEST_SLOTS * QUEST_FIELDS; i++) quest_state[i] = v[i];
            quest_state_valid = 1;
        }
        fclose(f);
    }
    quest_state_loaded = 1;
}

static void quest_state_write(void) {
    char path[320], text[256];
    int n = 0, i;
    data_file_path(path, sizeof(path), QUESTS_FILE);
    for (i = 0; i < QUEST_SLOTS * QUEST_FIELDS; i++) {
        n += snprintf(text + n, sizeof(text) - (size_t)n, "%.0f%s",
                      quest_state[i], (i + 1 < QUEST_SLOTS * QUEST_FIELDS) ? " " : "\n");
    }
    file_write_atomic(path, text);
}

void net_save_quest_state(double t0, double p0, double n0, double x0,
                          double t1, double p1, double n1, double x1,
                          double t2, double p2, double n2, double x2) {
    quest_state[0] = t0;  quest_state[1] = p0;  quest_state[2] = n0;  quest_state[3] = x0;
    quest_state[4] = t1;  quest_state[5] = p1;  quest_state[6] = n1;  quest_state[7] = x1;
    quest_state[8] = t2;  quest_state[9] = p2;  quest_state[10] = n2; quest_state[11] = x2;
    quest_state_loaded = 1;
    quest_state_valid = 1;
    quest_state_write();
}

double net_load_quest_state(double slot, double field) {
    int s = (int)slot, f = (int)field;
    quest_state_read();
    if (s < 0 || s >= QUEST_SLOTS || f < 0 || f >= QUEST_FIELDS) return 0;
    return quest_state[s * QUEST_FIELDS + f];
}

double net_quest_has_state(void) {
    quest_state_read();
    return quest_state_valid;
}
