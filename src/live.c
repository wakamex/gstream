#include "live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image_write.h"

void live_init(live *l) {
    memset(l, 0, sizeof *l);
    l->lock = SDL_CreateMutex();
    l->view_version = -1;
}

void live_free(live *l) { SDL_DestroyMutex(l->lock); }

typedef struct {
    live *l;
    const char *client_id, *dir;
    twitch_session *session;
    SDL_Mutex *session_lock;
    void (*wake)(void);
} refresh_job;

static void refresh(void *user) {
    refresh_job *j = user;
    live *l = j->l;
    static twitch_stream fresh[LIVE_MAX];  // (one refresh runs at a time)
    twitch_session s;
    SDL_LockMutex(j->session_lock);
    s = *j->session;
    SDL_UnlockMutex(j->session_lock);
    char before[sizeof s.access], message[160];
    SDL_strlcpy(before, s.access, sizeof before);
    int n = 0;
    twitch_error e = twitch_followed_live(j->client_id, &s, fresh, LIVE_MAX, &n, message, sizeof message);
    if (strcmp(before, s.access)) {  // renewed on the way
        SDL_LockMutex(j->session_lock);
        *j->session = s;
        SDL_UnlockMutex(j->session_lock);
        twitch_session_save(j->dir, &s);
    }
    SDL_LockMutex(l->lock);
    l->refreshing = false;
    l->last_error = e;
    if (e == TW_OK) {
        memcpy(l->all, fresh, sizeof *fresh * (size_t)n);
        l->count = n, l->loaded = true, l->stale = false, l->error[0] = 0, l->version++;
        SDL_Time now;
        SDL_DateTime dt;
        if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true)) SDL_snprintf(l->updated_at, sizeof l->updated_at, "%02d:%02d", dt.hour, dt.minute);
    } else {
        l->stale = l->loaded;
        SDL_strlcpy(l->error, message, sizeof l->error);
        l->version++;
    }
    SDL_UnlockMutex(l->lock);
    if (j->wake) j->wake();
    free(j);
}

void live_poll(live *l, gs_jobs *jobs, const char *client_id, twitch_session *s, SDL_Mutex *session_lock, const char *dir, void (*wake)(void)) {
    if (l->demo) return;
    SDL_LockMutex(l->lock);
    bool due = !l->refreshing && SDL_GetTicks() >= l->next_refresh;
    if (due) l->refreshing = true, l->next_refresh = SDL_GetTicks() + LIVE_REFRESH_MS;
    SDL_UnlockMutex(l->lock);
    if (!due) return;
    refresh_job *j = malloc(sizeof *j);
    if (!j) return;
    *j = (refresh_job){ l, client_id, dir, s, session_lock, wake };
    gs_jobs_add(jobs, refresh, j);
}

void live_refresh_now(live *l) {
    SDL_LockMutex(l->lock);
    l->next_refresh = 0;
    SDL_UnlockMutex(l->lock);
}

// ---- The view ----

static live *sorting;  // (qsort has no context argument; the view is made on one thread)

static int compare(const void *a, const void *b) {
    const twitch_stream *x = &sorting->all[*(const int *)a], *y = &sorting->all[*(const int *)b];
    switch (sorting->sort) {
    case SORT_CHANNEL: return SDL_strcasecmp(x->name, y->name);
    case SORT_CATEGORY: {
        int c = SDL_strcasecmp(x->category, y->category);
        return c ? c : y->viewers - x->viewers;
    }
    case SORT_STARTED: return strcmp(y->started_at, x->started_at);  // (RFC 3339 in UTC sorts as text) the newest first
    default: return y->viewers - x->viewers;
    }
}

// Whether a channel matches the filter: every word of it appears, ignoring case, in its name, login,
// title or category.
static bool matches(const twitch_stream *s, const char *filter) {
    char word[128];
    for (const char *p = filter; *p;) {
        while (*p == ' ') p++;
        size_t n = 0;
        while (p[n] && p[n] != ' ' && n + 1 < sizeof word) word[n] = p[n], n++;
        word[n] = 0;
        p += n;
        if (n && !SDL_strcasestr(s->name, word) && !SDL_strcasestr(s->login, word) && !SDL_strcasestr(s->title, word) && !SDL_strcasestr(s->category, word))
            return false;
    }
    return true;
}

void live_update_view(live *l) {
    SDL_LockMutex(l->lock);
    l->shown = 0;
    for (int i = 0; i < l->count; i++)
        if ((l->mature || !l->all[i].mature) && matches(&l->all[i], l->filter)) l->view[l->shown++] = i;
    sorting = l;
    qsort(l->view, (size_t)l->shown, sizeof *l->view, compare);
    l->view_version = l->version;
    SDL_UnlockMutex(l->lock);
}

// ---- The demo ----

static void append(void *ctx, void *data, int size) {
    struct { uint8_t *p; size_t n, cap; } *b = ctx;
    if (b->n + (size_t)size > b->cap) b->p = realloc(b->p, b->cap = (b->n + (size_t)size) * 2);
    memcpy(b->p + b->n, data, (size_t)size), b->n += (size_t)size;
}

void live_demo(live *l, gs_images *images) {
    static const char *const titles[] = {
        "Ranked grind to Masters, then viewer games",
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE9\x85\x8D\xE4\xBF\xA1 \xE3\x82\x86\xE3\x81\xA3\xE3\x81\x8F\xE3\x82\x8A",  // 日本語の配信 ゆっくり
        "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 \xEB\xB0\xA9\xEC\x86\xA1 \xE2\x80\x94 \xEC\x98\xA4\xEB\x8A\x98\xEC\x9D\x80 \xEA\xB3\xB5\xED\x8F\xAC \xEA\xB2\x8C\xEC\x9E\x84",  // 한국어 방송 — 오늘은 공포 게임
        "\xD8\xA8\xD8\xAB \xD9\x85\xD8\xA8\xD8\xA7\xD8\xB4\xD8\xB1: \xD8\xAA\xD8\xAD\xD8\xAF\xD9\x8A \xD8\xA7\xD9\x84\xD9\x8A\xD9\x88\xD9\x85 \xF0\x9F\x8E\xAE",  // بث مباشر: تحدي اليوم 🎮
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80 \xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\x9F\xE0\xA5\x8D\xE0\xA4\xB0\xE0\xA5\x80\xE0\xA4\xAE \xF0\x9F\x94\xA5",  // हिन्दी स्ट्रीम 🔥
        "\xF0\x9F\x94\xB4 LIVE \xF0\x9F\x94\xB4 !drops !merch \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD chill vibes \xE2\x9D\xA4\xEF\xB8\x8F",
        "A very long title that goes on and on past the width of any reasonable list, so it has to be cut short",
        "\xD0\x9F\xD1\x80\xD0\xBE\xD1\x85\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBD\xD0\xB0 \xD1\x80\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xBE\xD0\xBC",  // Прохождение на русском
        "\xE4\xB8\xAD\xE6\x96\x87\xE7\x9B\xB4\xE6\x92\xAD | \xE4\xBB\x8A\xE5\xA4\xA9\xE7\x8E\xA9\xE4\xBB\x80\xE4\xB9\x88",  // 中文直播 | 今天玩什么
        "Speedrun practice: any% glitchless",
    };
    static const char *const categories[] = { "Just Chatting", "League of Legends", "Minecraft", "Elden Ring", "Counter-Strike", "Art", "Music", "Valorant" };
    SDL_LockMutex(l->lock);
    l->demo = l->loaded = true;
    l->count = 100;
    SDL_strlcpy(l->updated_at, "demo", sizeof l->updated_at);
    for (int i = 0; i < l->count; i++) {
        twitch_stream *s = &l->all[i];
        memset(s, 0, sizeof *s);
        SDL_snprintf(s->login, sizeof s->login, "demo_channel_%02d", i);
        SDL_snprintf(s->name, sizeof s->name, i % 7 == 3 ? "\xE3\x83\x81\xE3\x83\xA3\xE3\x83\xB3\xE3\x83\x8D\xE3\x83\xAB%02d" : "DemoChannel%02d", i);
        SDL_strlcpy(s->title, titles[i % SDL_arraysize(titles)], sizeof s->title);
        SDL_strlcpy(s->category, categories[i % SDL_arraysize(categories)], sizeof s->category);
        s->viewers = (int)(200000 / (1 + i * i * 0.3) + 13 * i);
        s->mature = i % 9 == 4;
        SDL_snprintf(s->started_at, sizeof s->started_at, "2026-10-08T%02d:%02d:00Z", 6 + i % 12, (i * 7) % 60);
        // A thumbnail: a JPEG of a gradient in the channel's colour, at Twitch's 320 x 180.
        SDL_snprintf(s->thumbnail, sizeof s->thumbnail, "demo:thumbnail/%02d/{width}x{height}", i);
        char url[64];
        twitch_thumbnail_url(s->thumbnail, 320, 180, url, sizeof url);
        static uint8_t px[320 * 180 * 3];
        for (int y = 0; y < 180; y++)
            for (int x = 0; x < 320; x++) {
                uint8_t *p = px + 3 * (y * 320 + x);
                p[0] = (uint8_t)((i * 53 + x / 2) & 255), p[1] = (uint8_t)((i * 97 + y) & 255), p[2] = (uint8_t)(((i * 31) ^ (x + y)) & 255);
            }
        struct { uint8_t *p; size_t n, cap; } buf = { 0 };
        if (stbi_write_jpg_to_func(append, &buf, 320, 180, 3, px, 80)) gs_images_put(images, url, buf.p, buf.n);
        free(buf.p);
    }
    l->version++;
    SDL_UnlockMutex(l->lock);
}
