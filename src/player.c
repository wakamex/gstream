#include "player.h"

#include <stdlib.h>
#include <string.h>

#include "gs_mix.h"

void player_init(player *p, SDL_Renderer *r, void (*wake)(void)) {
    memset(p, 0, sizeof *p);
    p->lock = SDL_CreateMutex();
    p->renderer = r, p->wake = wake, p->volume = 0.8f;
    SDL_strlcpy(p->quality, "best", sizeof p->quality);
    gs_mix_set_volume(p->volume);
}

void player_free(player *p) {
    player_stop(p);
    SDL_DestroyMutex(p->lock);
}

// gs_live's hooks, on its threads.
typedef struct { player *p; int generation; char login[64], quality[64]; } live_user;

static bool renew(void *user, char *url, size_t size) {
    live_user *u = user;
    twitch_playback pb = { 0 };
    char msg[200];
    if (twitch_resolve(u->login, u->quality, false, &pb, msg, sizeof msg) != TW_OK) return false;
    SDL_strlcpy(url, pb.url, size);
    return true;
}

// Twitch marks the segments of a stitched ad with a date range of its class, and titles them "Amazon".
static void segment(void *user, const gs_hls_segment *s) {
    live_user *u = user;
    bool ad = SDL_strstr(s->tags, "twitch-stitched-ad") || SDL_strstr(s->tags, "stitched-ad") || !SDL_strcmp(s->title, "Amazon");
    SDL_LockMutex(u->p->lock);
    bool changed = u->generation == u->p->generation && u->p->ad != ad;
    if (changed) u->p->ad = ad;
    SDL_UnlockMutex(u->p->lock);
    if (changed && u->p->wake) u->p->wake();
}

static void changed(void *user) {
    live_user *u = user;
    if (u->p->wake) u->p->wake();
}

typedef struct { player *p; int generation; char login[64], quality[64]; bool video; } resolve_job;

static void resolve(void *user) {
    resolve_job *j = user;
    player *p = j->p;
    twitch_playback pb = { 0 };
    char msg[200];
    twitch_error e = twitch_resolve(j->login, j->quality, false, &pb, msg, sizeof msg);
    bool audio_only = !SDL_strcmp(pb.quality, "audio_only");
    gs_live *live = NULL;
    live_user *u = e == TW_OK ? malloc(sizeof *u) : NULL;
    if (e == TW_OK && !u) e = TW_UNKNOWN, SDL_strlcpy(msg, "out of memory", sizeof msg);
    if (e == TW_OK) {
        *u = (live_user){ p, j->generation, "", "" };
        SDL_strlcpy(u->login, j->login, sizeof u->login), SDL_strlcpy(u->quality, j->quality, sizeof u->quality);
        static const char *const headers[] = { "Referer: https://player.twitch.tv", "Origin: https://player.twitch.tv", NULL };
        gs_live_config c = { .url = pb.url, .agent = "Mozilla/5.0", .headers = headers, .video = j->video && !audio_only, .renderer = p->renderer,
                             .renew = renew, .segment = segment, .changed = changed, .user = u };
        live = gs_live_start(&c);
        if (!live) e = TW_UNKNOWN, SDL_strlcpy(msg, "the player could not start", sizeof msg);
    }
    SDL_LockMutex(p->lock);
    if (j->generation != p->generation) {  // stopped or replaced meanwhile
        SDL_UnlockMutex(p->lock);
        gs_live_stop(live);
        free(u), free(j);
        return;
    }
    if (!live) free(u), u = NULL;
    p->live = live, p->live_user = u, p->error = e;
    p->state = e == TW_OK ? PLAYER_PLAYING : PLAYER_FAILED;
    SDL_strlcpy(p->playing, e == TW_OK ? pb.quality : "", sizeof p->playing);
    SDL_strlcpy(p->message, e == TW_OK ? "" : msg, sizeof p->message);
    SDL_UnlockMutex(p->lock);
    if (p->wake) p->wake();
    free(j);
}

void player_start(player *p, gs_jobs *jobs, const char *login, const char *name, const char *quality, bool video) {
    player_stop(p);
    resolve_job *j = malloc(sizeof *j);
    if (!j) return;
    SDL_LockMutex(p->lock);
    p->generation++;
    p->state = PLAYER_RESOLVING, p->ad = false, p->video = video, p->message[0] = p->playing[0] = 0;
    SDL_strlcpy(p->login, login, sizeof p->login), SDL_strlcpy(p->name, name, sizeof p->name);
    if (quality) SDL_strlcpy(p->quality, quality, sizeof p->quality);
    *j = (resolve_job){ p, p->generation, "", "", video };
    SDL_strlcpy(j->login, login, sizeof j->login), SDL_strlcpy(j->quality, p->quality, sizeof j->quality);
    SDL_UnlockMutex(p->lock);
    gs_jobs_add(jobs, resolve, j);
}

void player_stop(player *p) {
    SDL_LockMutex(p->lock);
    gs_live *live = p->live;
    void *user = p->live_user;
    p->live = NULL, p->live_user = NULL, p->generation++, p->state = PLAYER_IDLE, p->ad = false;
    SDL_UnlockMutex(p->lock);
    gs_live_stop(live);
    free(user);
}

void player_set_volume(player *p, float volume, bool muted) {
    p->volume = volume, p->muted = muted;
    gs_mix_set_volume(muted ? 0 : volume);
}
