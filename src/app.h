// The app's state, shared by main.c (events, frames, options) and views.c (drawing).
#pragma once
#include <SDL3/SDL.h>

#include "gs_image.h"
#include "gs_jobs.h"
#include "gs_oauth.h"
#include "gs_pace.h"
#include "gs_stats.h"
#include "gs_text.h"
#include "gs_ui.h"
#include "live.h"
#include "twitch.h"

typedef enum { SIGNED_OUT, SIGNING_IN, SIGNED_IN } auth_state;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    gs_ui *ui;
    gs_fontset *fonts;
    gs_pace pace;
    gs_stats stats;
    gs_jobs *jobs;
    gs_images *images;
    char dir[1024];  // the data folder, ending with a separator

    // Signing in (the session is shared with workers under lock).
    SDL_Mutex *lock;
    auth_state auth;
    twitch_session session;
    gs_oauth_device device;
    char auth_message[200];
    bool auth_busy;
    SDL_AtomicInt auth_cancel;

    live live;
    int selected;          // in the list's view
    bool compact;          // rows instead of cards
    int sort;              // a live_sort
    char playing[64];      // the channel chosen to watch
    char playing_name[128];

    gs_window_state window;
    bool persist, window_changed;
    uint64_t next_save;

    // Options and test modes.
    bool show_stats, demo, software;
    const char *shot;
    double shot_at, quit_at;
    const char *stats_file;
    uint64_t started, stats_next;
    int frames;
} app;

void views_draw(app *a);
void sign_in_start(app *a);
void sign_in_cancel(app *a);
void sign_out(app *a);
