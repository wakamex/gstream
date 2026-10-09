#include "player.h"

#include <stdlib.h>
#include <string.h>

#include "gs_mix.h"

void player_init(player *p, SDL_Renderer *r, void (*wake)(void)) {
    memset(p, 0, sizeof *p);
    p->lock = SDL_CreateMutex();
    p->renderer = r, p->wake = wake, p->volume = 0.8f;
    SDL_strlcpy(p->quality, "auto", sizeof p->quality);
    gs_mix_set_volume(p->volume);
}

void player_free(player *p) {
    player_stop(p);
    SDL_DestroyMutex(p->lock);
}

// gs_live's hooks, on its threads.
typedef struct { player *p; int generation; char login[64], quality[64]; } live_user;

static bool is_auto(const char *quality) { return !SDL_strcasecmp(quality, "auto"); }

// A fresh playlist URL of the kind playing: the master playlist when adaptive.
static bool renew(void *user, char *url, size_t size) {
    live_user *u = user;
    twitch_playback pb = { 0 };
    char msg[200];
    bool adaptive = is_auto(u->quality);
    if (twitch_resolve(u->login, adaptive ? "best" : u->quality, false, &pb, msg, sizeof msg) != TW_OK) return false;
    SDL_strlcpy(url, adaptive && pb.master[0] ? pb.master : pb.url, size);
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

// What the viewer is told when a channel cannot play, by the kind of failure; Twitch's own words
// follow where the kind alone does not explain it.
static void failure_text(twitch_error e, const char *said, char *out, size_t size) {
    switch (e) {
    case TW_OFFLINE: SDL_strlcpy(out, "This channel is offline.", size); break;
    case TW_LOGIN_REQUIRED: SDL_strlcpy(out, "Twitch wants you to sign in again to watch this.", size); break;
    case TW_SUBSCRIBER_ONLY: SDL_strlcpy(out, "This stream is for the channel's subscribers only.", size); break;
    case TW_GEO_BLOCKED: SDL_strlcpy(out, "This stream is not available in your region.", size); break;
    case TW_AGE_RESTRICTED: SDL_strlcpy(out, "This stream is age-restricted and plays only on Twitch's site.", size); break;
    case TW_RATE_LIMITED: SDL_strlcpy(out, "Twitch is limiting requests. Try again in a minute.", size); break;
    case TW_UNSUPPORTED: SDL_snprintf(out, size, "Twitch would not play this stream here (%s).", said); break;
    case TW_NETWORK: SDL_snprintf(out, size, "Twitch did not answer (%s). Check the connection and try again.", said); break;
    default: SDL_snprintf(out, size, "This stream could not play: %s.", said); break;
    }
}

static long long rank(const gs_hls_variant *v) { return v->audio_only ? -1 : (long long)v->height * 1000000000LL + SDL_lround(v->fps) * 1000000LL + v->bandwidth; }

static void resolve(void *user) {
    resolve_job *j = user;
    player *p = j->p;
    twitch_playback pb = { 0 };
    char msg[200];
    bool adaptive = is_auto(j->quality) && j->video;
    twitch_error e = twitch_resolve(j->login, adaptive ? "best" : j->quality, false, &pb, msg, sizeof msg);
    adaptive = adaptive && pb.master[0];
    bool audio_only = !SDL_strcmp(pb.quality, "audio_only");
    gs_live *live = NULL;
    live_user *u = e == TW_OK ? malloc(sizeof *u) : NULL;
    if (e == TW_OK && !u) e = TW_UNKNOWN, SDL_strlcpy(msg, "out of memory", sizeof msg);
    if (e == TW_OK) {
        *u = (live_user){ p, j->generation, "", "" };
        SDL_strlcpy(u->login, j->login, sizeof u->login), SDL_strlcpy(u->quality, j->quality, sizeof u->quality);
        static const char *const headers[] = { "Referer: https://player.twitch.tv", "Origin: https://player.twitch.tv", NULL };
        gs_live_config c = { .url = adaptive ? pb.master : pb.url, .agent = "Mozilla/5.0", .headers = headers, .video = j->video && !audio_only,
                             .renderer = p->renderer, .start_bandwidth = p->bandwidth, .renew = renew, .segment = segment, .changed = changed, .user = u };
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
    if (e == TW_OK) p->message[0] = 0;
    else failure_text(e, msg, p->message, sizeof p->message);
    // The renditions, best first (Twitch lists them in a different order each time).
    int order[16], n = 0;
    for (int i = 0; e == TW_OK && i < pb.count && n < 16; i++) {
        int k = n++;
        for (; k > 0 && rank(&pb.variants[order[k - 1]]) < rank(&pb.variants[i]); k--) order[k] = order[k - 1];
        order[k] = i;
    }
    p->quality_count = n;
    for (int i = 0; i < n; i++) twitch_variant_name(&pb.variants[order[i]], p->qualities[i], sizeof p->qualities[i]);
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
    if (SDL_strcmp(p->login, login)) p->quality_count = 0;
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
    double measured = live ? gs_live_get_info(live).bandwidth : 0;
    if (measured > 0) p->bandwidth = measured;  // (where the next channel starts)
    void *user = p->live_user;
    p->live = NULL, p->live_user = NULL, p->generation++, p->state = PLAYER_IDLE, p->ad = false;
    SDL_UnlockMutex(p->lock);
    gs_live_stop(live);
    free(user);
}

void player_retry(player *p, gs_jobs *jobs) {
    char login[64], name[128];
    SDL_LockMutex(p->lock);
    SDL_strlcpy(login, p->login, sizeof login), SDL_strlcpy(name, p->name, sizeof name);
    bool video = p->video;
    SDL_UnlockMutex(p->lock);
    player_start(p, jobs, login, name, NULL, video);
}

void player_set_volume(player *p, float volume, bool muted) {
    p->volume = volume, p->muted = muted;
    gs_mix_set_volume(muted ? 0 : volume);
}

void player_play_url(player *p, const char *url, bool video) {
    player_stop(p);
    live_user *u = malloc(sizeof *u);
    if (!u) return;
    SDL_LockMutex(p->lock);
    p->generation++;
    *u = (live_user){ p, p->generation, "", "" };
    gs_live_config c = { .url = url, .video = video, .renderer = p->renderer, .segment = segment, .changed = changed, .user = u };
    p->live = gs_live_start(&c);
    p->live_user = p->live ? u : NULL;
    p->video = video, p->state = p->live ? PLAYER_PLAYING : PLAYER_FAILED, p->quality_count = 0;
    SDL_strlcpy(p->name, url, sizeof p->name), SDL_strlcpy(p->login, "url", sizeof p->login), SDL_strlcpy(p->playing, "", sizeof p->playing);
    SDL_strlcpy(p->message, p->live ? "" : "the player could not start", sizeof p->message);
    SDL_UnlockMutex(p->lock);
    if (!p->live) free(u);
}
