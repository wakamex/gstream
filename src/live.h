// The followed channels that are live: refreshed from Twitch on a worker every 90 seconds (or made
// up, with --demo), and the filtered, sorted view the list draws.
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "gs_image.h"
#include "gs_jobs.h"
#include "twitch.h"

#define LIVE_MAX 500
#define LIVE_REFRESH_MS 90000

typedef enum { SORT_VIEWERS, SORT_CHANNEL, SORT_CATEGORY, SORT_STARTED } live_sort;

typedef struct {
    SDL_Mutex *lock;
    twitch_stream all[LIVE_MAX];  // as Twitch last sent them
    int count;
    bool loaded, refreshing, stale, demo;
    char updated_at[16];          // the local time of the last good refresh, "14:05"
    char error[160];              // why the last refresh failed, when it did
    uint64_t next_refresh;        // SDL_GetTicks time
    twitch_error last_error;
    // The view: indexes into all[], filtered and sorted.
    int view[LIVE_MAX];
    int shown;
    char filter[128];
    live_sort sort;
    bool mature;                  // show channels marked for mature audiences
    int version, view_version;    // the data's version, and the version the view was made from
} live;

void live_init(live *l);
void live_free(live *l);
// Starts a refresh on a worker when one is due; the session may be renewed on the way (saved to dir).
// `wake` is called (from the worker) when the data changed.
void live_poll(live *l, gs_jobs *jobs, const char *client_id, twitch_session *s, SDL_Mutex *session_lock, const char *dir, void (*wake)(void));
void live_refresh_now(live *l);
// 100 made-up channels with titles in many scripts, and generated thumbnails added to images.
void live_demo(live *l, gs_images *images);
// Brings the view up to date with the data, the filter, the sort and the mature setting.
void live_update_view(live *l);
