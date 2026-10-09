// Watching a channel: its stream resolved on a worker, then played by gesso's gs_live, with the playlist
// URL resolved again when Twitch's token runs out, and ads noticed from the playlist's markers.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "gs_jobs.h"
#include "gs_live.h"
#include "twitch.h"

typedef enum { PLAYER_IDLE, PLAYER_RESOLVING, PLAYER_PLAYING, PLAYER_FAILED } player_state;

typedef struct {
    SDL_Mutex *lock;
    player_state state;
    char login[64], name[128];
    char quality[64];       // asked for: "auto" (adaptive), "best", "audio_only", "720p60"...
    char playing[64];       // the rendition playing, such as "1080p60" (the first one, when adaptive)
    char message[200];      // why it failed, for the viewer
    twitch_error error;
    char qualities[16][32]; // the channel's renditions, best first, audio_only last
    int quality_count;
    bool video, ad;         // video wanted; an ad is playing (from the playlist's markers)
    double bandwidth;       // the last throughput estimate (bits a second, 0 when none), where adaptive play starts
    int generation;         // bumped by each start and stop, so a late resolve is dropped
    gs_live *live;
    void *live_user;        // gs_live's hooks' context, freed after it stops
    SDL_Renderer *renderer;
    void (*wake)(void);
    float volume;           // 0 to 1
    bool muted;
} player;

void player_init(player *p, SDL_Renderer *r, void (*wake)(void));
void player_free(player *p);
void player_start(player *p, gs_jobs *jobs, const char *login, const char *name, const char *quality, bool video);
void player_stop(player *p);
void player_play_url(player *p, const char *url, bool video);  // an HLS media playlist, without Twitch
void player_set_volume(player *p, float volume, bool muted);
void player_retry(player *p, gs_jobs *jobs);  // starts the same channel again
