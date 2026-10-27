// gstream: a native Twitch client on gesso. Sign in, see which channels you follow are live, watch.
// Its options are in `usage` below. In the list: / or Ctrl+F searches, Up/Down/Page Up/Page Down/
// Home/End move, Enter or a click watches; F1 shows the performance overlay; F11 or Alt+Enter is
// full screen. The window draws only when something changes.
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gs_mix.h"
#include "gs_update.h"
#include "gs_video.h"
#include "stb_image_write.h"

static const char usage[] =
    "gstream                                   sign in and watch the channels you follow\n"
    "  --sign-in                               sign in here in the terminal, then quit\n"
    "  --sign-out                              forget the saved session\n"
    "  --api live                              print the followed channels that are live\n"
    "  --api category NAME                     print the live streams in a category, such as \"Dota 2\"\n"
    "  --json                                  print --api live and --api category as one JSON object a line\n"
    "  --api resolve CHANNEL [QUALITY]         print a channel's stream (QUALITY such as best, 720p60, audio_only)\n"
    "  --play CHANNEL                          start watching a channel\n"
    "  --quality Q, --audio-only               the rendition to play (auto, best, 720p60, audio_only...)\n"
    "  --url URL                               play an HLS media playlist directly, for testing\n"
    "  --resume                                go on with the channel playing when gstream last quit\n"
    "  --demo                                  100 made-up channels, for trying the interface offline\n"
    "  --software                              decode video in software\n"
    "  --mute                                  start muted\n"
    "  --data DIR                              where the session and settings live\n"
    "  --shot F.png [--at S]                   render one frame at S seconds, headless, and quit; prints a\n"
    "                                          JSON line with the time the stream gives that frame\n"
    "  --stats FILE [--quit S]                 add the performance overlay's text to FILE each second;\n"
    "                                          quit after S seconds\n";

static Uint32 wake_event;
static SDL_TimerID wake_timer;

// Called from any thread when something the window shows has changed.
static void wake(void) {
    SDL_Event e = { .type = wake_event };
    SDL_PushEvent(&e);
}

static Uint32 SDLCALL quit_later(void *user, SDL_TimerID id, Uint32 interval) {
    (void)user, (void)id, (void)interval;
    SDL_Event e = { .type = SDL_EVENT_QUIT };
    SDL_PushEvent(&e);
    return 0;
}

static Uint32 SDLCALL wake_later(void *user, SDL_TimerID id, Uint32 interval) {
    (void)user, (void)id, (void)interval;
    wake();
    return 0;
}

// ---- Signing in ----

// --sign-in: the device code flow in the terminal.
static bool sign_in_here(const char *dir) {
    gs_oauth_client c = twitch_oauth_client(GSTREAM_CLIENT_ID);
    gs_oauth_device d = { 0 };
    char msg[256];
    if (!GSTREAM_CLIENT_ID[0]) return printf("no Twitch Client ID was built in (-Dclient-id)\n"), false;
    if (gs_oauth_start(&c, &d, msg, sizeof msg) != GS_OAUTH_OK) return printf("could not start: %s\n", msg), false;
    printf("Open %s and enter the code %s\n", d.verification_uri, d.user_code);
    fflush(stdout);
    uint64_t until = SDL_GetTicks() + (uint64_t)(d.expires_in * 1000);
    gs_oauth_token t = { 0 };
    for (;;) {
        SDL_Delay((Uint32)(d.interval * 1000));
        gs_oauth_result r = gs_oauth_poll(&c, &d, &t, msg, sizeof msg);
        if (r == GS_OAUTH_OK) break;
        if ((r != GS_OAUTH_PENDING && r != GS_OAUTH_SLOW_DOWN && r != GS_OAUTH_NETWORK) || SDL_GetTicks() > until)
            return printf("not signed in: %s\n", msg), false;
    }
    twitch_session s;
    twitch_error e = twitch_session_from(GSTREAM_CLIENT_ID, &t, &s, msg, sizeof msg);
    SDL_memset(&t, 0, sizeof t);
    if (e != TW_OK || !twitch_session_save(dir, &s)) return printf("not signed in: %s\n", e != TW_OK ? msg : "the session could not be saved"), false;
    printf("signed in as %s\n", s.login);
    return true;
}

// The window's sign-in, on a worker: a code, then polling until the user approves or it runs out.
static void sign_in_job(void *user) {
    app *a = user;
    gs_oauth_client c = twitch_oauth_client(GSTREAM_CLIENT_ID);
    gs_oauth_device d = { 0 };
    char msg[200];
    gs_oauth_result r = gs_oauth_start(&c, &d, msg, sizeof msg);
    SDL_LockMutex(a->lock);
    if (r == GS_OAUTH_OK) a->device = d, a->auth_message[0] = 0;
    else a->auth = SIGNED_OUT, SDL_strlcpy(a->auth_message, msg, sizeof a->auth_message);
    a->auth_busy = false;
    SDL_UnlockMutex(a->lock);
    wake();
    if (r != GS_OAUTH_OK) return;
    uint64_t until = SDL_GetTicks() + (uint64_t)(d.expires_in * 1000);
    gs_oauth_token t = { 0 };
    for (;;) {
        for (int i = 0; i < (int)(d.interval * 10); i++) {  // (checking for Cancel every 100 ms)
            if (SDL_GetAtomicInt(&a->auth_cancel)) return;
            SDL_Delay(100);
        }
        r = gs_oauth_poll(&c, &d, &t, msg, sizeof msg);
        if (r == GS_OAUTH_OK) break;
        if ((r != GS_OAUTH_PENDING && r != GS_OAUTH_SLOW_DOWN && r != GS_OAUTH_NETWORK) || SDL_GetTicks() > until) {
            SDL_LockMutex(a->lock);
            a->auth = SIGNED_OUT;
            SDL_strlcpy(a->auth_message, SDL_GetTicks() > until ? "The code ran out; sign in again." : msg, sizeof a->auth_message);
            SDL_UnlockMutex(a->lock);
            wake();
            return;
        }
    }
    twitch_session s;
    twitch_error e = twitch_session_from(GSTREAM_CLIENT_ID, &t, &s, msg, sizeof msg);
    SDL_memset(&t, 0, sizeof t);
    SDL_LockMutex(a->lock);
    if (e == TW_OK && twitch_session_save(a->dir, &s)) a->session = s, a->auth = SIGNED_IN, a->auth_message[0] = 0;
    else a->auth = SIGNED_OUT, SDL_strlcpy(a->auth_message, e == TW_OK ? "The session could not be saved." : msg, sizeof a->auth_message);
    SDL_UnlockMutex(a->lock);
    live_refresh_now(&a->live);
    wake();
}

void sign_in_start(app *a) {
    SDL_LockMutex(a->lock);
    a->auth = SIGNING_IN, a->auth_busy = true, a->device = (gs_oauth_device){ 0 };
    SDL_UnlockMutex(a->lock);
    SDL_SetAtomicInt(&a->auth_cancel, 0);
    gs_jobs_add(a->jobs, sign_in_job, a);
}

void sign_in_cancel(app *a) {
    SDL_SetAtomicInt(&a->auth_cancel, 1);
    SDL_LockMutex(a->lock);
    a->auth = SIGNED_OUT, a->auth_message[0] = 0;
    SDL_UnlockMutex(a->lock);
}

void watch(app *a, const char *login, const char *name) {
    player_start(&a->player, a->jobs, login, name, a->audio_only ? "audio_only" : a->quality, !a->audio_only);
}

void sign_out(app *a) {
    twitch_session_erase(a->dir);
    SDL_LockMutex(a->lock);
    a->auth = SIGNED_OUT, a->session = (twitch_session){ 0 };
    SDL_UnlockMutex(a->lock);
    SDL_LockMutex(a->live.lock);
    a->live.count = 0, a->live.loaded = false, a->live.version++;
    SDL_UnlockMutex(a->live.lock);
    player_stop(&a->player);
    a->theater = false;
}

// At launch: the saved session, checked (and renewed if it expired) on a worker.
static void restore_job(void *user) {
    app *a = user;
    twitch_session s;
    SDL_LockMutex(a->lock);
    s = a->session;
    SDL_UnlockMutex(a->lock);
    char msg[200];
    twitch_error e = twitch_validate(GSTREAM_CLIENT_ID, &s, msg, sizeof msg);
    if (e == TW_LOGIN_REQUIRED) e = twitch_refresh(GSTREAM_CLIENT_ID, &s, msg, sizeof msg), e = e == TW_OK ? twitch_validate(GSTREAM_CLIENT_ID, &s, msg, sizeof msg) : e;
    SDL_LockMutex(a->lock);
    if (e == TW_OK) a->session = s, twitch_session_save(a->dir, &s);
    else if (e == TW_LOGIN_REQUIRED) a->auth = SIGNED_OUT, SDL_strlcpy(a->auth_message, "Your session ended; sign in again.", sizeof a->auth_message), twitch_session_erase(a->dir);
    SDL_UnlockMutex(a->lock);  // (on a network failure the saved session is kept and tried again with the list)
    wake();
}

// --api: one answer from Twitch, printed.
// A string as a JSON string, quotes included.
static void print_json(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

// Unix seconds as UTC, such as 2026-10-08T17:26:03.620Z.
static void utc_text(double t, char *out, size_t size) {
    SDL_DateTime d;
    SDL_TimeToDateTime((SDL_Time)(t * 1e9), &d, false);
    snprintf(out, size, "%04d-%02d-%02dT%02d:%02d:%06.3fZ", d.year, d.month, d.day, d.hour, d.minute, d.second + d.nanosecond / 1e9);
}

static void print_streams(const twitch_stream *streams, int n, bool json) {
    for (int i = 0; i < n; i++) {
        const twitch_stream *s = &streams[i];
        if (!json) {
            printf("%7d  %-20s %-28.28s %s\n", s->viewers, s->login, s->category, s->title);
            continue;
        }
        printf("{\"login\":"), print_json(s->login), printf(",\"name\":"), print_json(s->name);
        printf(",\"viewers\":%d,\"started_at\":", s->viewers), print_json(s->started_at);
        printf(",\"category\":"), print_json(s->category), printf(",\"language\":"), print_json(s->language);
        printf(",\"title\":"), print_json(s->title), printf("}\n");
    }
    if (!json) printf("%d live\n", n);
}

static bool api(const char *dir, const char *kind, const char *arg, const char *quality, bool json) {
    char msg[256];
    if (!strcmp(kind, "resolve") && arg) {
        twitch_playback p = { 0 };
        twitch_error e = twitch_resolve(arg, quality ? quality : "best", false, &p, msg, sizeof msg);
        if (e != TW_OK) return printf("%s: %s\n", twitch_error_text(e), msg), false;
        for (int i = 0; i < p.count; i++) {
            char name[64];
            twitch_variant_name(&p.variants[i], name, sizeof name);
            printf("  %-12s %4dx%-4d %5.1f fps %6lld kbit/s  %s\n", name, p.variants[i].width, p.variants[i].height, p.variants[i].fps,
                   p.variants[i].bandwidth / 1000, p.variants[i].codecs);
        }
        printf("%s (token expires %lld)\n%s\n", p.quality, p.expires_at, p.url);
        return true;
    }
    bool category = !strcmp(kind, "category") && arg;
    if (!strcmp(kind, "live") || category) {
        twitch_session s;
        if (!twitch_session_load(dir, &s)) return fprintf(stderr, "not signed in (gstream --sign-in)\n"), false;
        static twitch_stream streams[LIVE_MAX];
        int n;
        char before[sizeof s.access];
        SDL_strlcpy(before, s.access, sizeof before);
        twitch_error e = category ? twitch_category_live(GSTREAM_CLIENT_ID, &s, arg, streams, LIVE_MAX, &n, msg, sizeof msg)
                                  : twitch_followed_live(GSTREAM_CLIENT_ID, &s, streams, LIVE_MAX, &n, msg, sizeof msg);
        if (strcmp(before, s.access)) twitch_session_save(dir, &s);  // renewed on the way
        if (e != TW_OK) return fprintf(stderr, "%s: %s\n", twitch_error_text(e), msg), false;
        print_streams(streams, n, json);
        return true;
    }
    return printf("%s", usage), false;
}

static void data_dir(const char *data, char *out, size_t size) {
    if (data) {
        size_t n = strlen(data);
#ifdef _WIN32
        const char *slash = "\\";
#else
        const char *slash = "/";
#endif
        snprintf(out, size, "%s%s", data, n && (data[n - 1] == '/' || data[n - 1] == '\\') ? "" : slash);
        SDL_CreateDirectory(data);
        return;
    }
    char *pref = SDL_GetPrefPath("wakamex", "gstream");  // (made if missing)
    snprintf(out, size, "%s", pref ? pref : "");
    SDL_free(pref);
}

// The last throughput estimate, in bits a second, kept between runs so adaptive play starts at a
// rendition the connection carried.
static double load_bandwidth(const app *a) {
    char path[1200];
    snprintf(path, sizeof path, "%sbandwidth.txt", a->dir);
    char *text = SDL_LoadFile(path, NULL);
    double bps = text ? SDL_strtod(text, NULL) : 0;
    SDL_free(text);
    return bps > 0 && bps < 1e11 ? bps : 0;
}

static void save_bandwidth(const app *a, double bps) {
    char path[1200], text[32];
    snprintf(path, sizeof path, "%sbandwidth.txt", a->dir);
    int n = snprintf(text, sizeof text, "%.0f\n", bps);
    SDL_SaveFile(path, text, (size_t)n);
}

// A build published with tools/publish-update keeps itself up to date from the update server, through
// gesso's gs_update: a manifest of the build's files signed with the publisher's Ed25519 key, whose
// public half is this. Any other build (a release, or one's own) never looks.
#define UPDATE_SERVER "http://updates.waka.mx/gstream"
static const uint8_t publisher[32] = {
    0xb9, 0x1a, 0x09, 0x55, 0xbc, 0x05, 0xbc, 0x25, 0x9f, 0xa8, 0x97, 0x5e, 0x47, 0x2a, 0x4a, 0x11,
    0xa8, 0xdb, 0xe3, 0x20, 0x87, 0x24, 0xbd, 0x8a, 0x33, 0xa0, 0xff, 0x08, 0x74, 0xac, 0x16, 0xd7,
};
#ifdef _WIN32
#define PROGRAM "gstream.exe"
#else
#define PROGRAM "gstream"
#endif

static void update_ready(void *user) { (void)user, wake(); }

// GSTREAM_UPDATE_FROM names another server, for trying it out.
static gs_update *update_start(app *a) {
#ifdef GSTREAM_BUILD
    const char *from = SDL_getenv("GSTREAM_UPDATE_FROM");
    char manifest[512];
    SDL_snprintf(manifest, sizeof manifest, "%s/manifest.txt", from && *from ? from : UPDATE_SERVER);
    char state[1100];
    SDL_snprintf(state, sizeof state, "%supdate.txt", a->dir);  // (the user's answer, and the build on trial)
    gs_update_config c = { .build = GSTREAM_BUILD, .manifest = manifest, .key = publisher, .program = PROGRAM, .agent = "gstream", .ready = update_ready, .state = state };
    return gs_update_start(&c);
#else
    (void)a;
    return NULL;
#endif
}

// The volume (0 to 1) and whether it is muted, kept between runs: "0.35 0".
static void load_volume(app *a) {
    char path[1200];
    snprintf(path, sizeof path, "%svolume.txt", a->dir);
    char *text = SDL_LoadFile(path, NULL), *end = NULL;
    double v = text ? SDL_strtod(text, &end) : -1;
    if (v >= 0 && v <= 1) a->volume = (float)v, a->muted = a->muted || SDL_atoi(end) == 1;  // (--mute mutes either way)
    SDL_free(text);
}

static void save_volume(const app *a) {
    char path[1200], text[32];
    snprintf(path, sizeof path, "%svolume.txt", a->dir);
    int n = snprintf(text, sizeof text, "%.2f %d\n", a->volume, a->muted);
    SDL_SaveFile(path, text, (size_t)n);
}

// The channel playing when gstream quits, for --resume (which the tools that replace a running
// gstream pass): its login, quality, video, full window and name, a line each.
static void resume_path(const app *a, char *out, size_t size) { snprintf(out, size, "%sresume.txt", a->dir); }

static void save_resume(app *a) {
    char path[1200], text[400];
    resume_path(a, path, sizeof path);
    player *p = &a->player;
    SDL_LockMutex(p->lock);
    bool playing = p->state != PLAYER_IDLE && SDL_strcmp(p->login, "url");
    int n = SDL_snprintf(text, sizeof text, "%s\n%s\n%d\n%d\n%s\n", p->login, p->quality, p->video, a->theater, p->name);
    SDL_UnlockMutex(p->lock);
    if (playing) SDL_SaveFile(path, text, (size_t)n);
    else SDL_RemovePath(path);
}

static void resume(app *a) {
    char path[1200];
    resume_path(a, path, sizeof path);
    char *text = SDL_LoadFile(path, NULL), *at = NULL;
    SDL_RemovePath(path);  // (used once)
    const char *login = text ? SDL_strtok_r(text, "\n", &at) : NULL, *quality = SDL_strtok_r(NULL, "\n", &at);
    const char *video = SDL_strtok_r(NULL, "\n", &at), *theater = SDL_strtok_r(NULL, "\n", &at), *name = SDL_strtok_r(NULL, "\n", &at);
    if (name) {
        player_start(&a->player, a->jobs, login, name, quality, SDL_atoi(video));
        a->theater = SDL_atoi(theater);
        if (a->auth == SIGNED_OUT) a->auth = SIGNED_IN;  // (as with --play: playback needs no account)
    }
    SDL_free(text);
}

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
    app *a = SDL_calloc(1, sizeof *a);
    *state = a;
    bool restarted = gs_update_restarted(argc, argv);  // (after an update, the window does not take the keyboard)
    bool json = false;
    const char *data = NULL, *api_kind = NULL, *api_arg = NULL, *api_quality = NULL, *play = NULL, *url = NULL;
    bool do_sign_in = false, do_sign_out = false, do_resume = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return printf("%s", usage), SDL_APP_SUCCESS;
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data = a->data = argv[++i];
        else if (!strcmp(argv[i], "--software")) a->software = true;
        else if (!strcmp(argv[i], "--json")) json = true;
        else if (!strcmp(argv[i], "--mute")) a->muted = true;
        else if (!strcmp(argv[i], "--demo")) a->demo = true;
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play = argv[++i];
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc) a->quality = argv[++i];
        else if (!strcmp(argv[i], "--audio-only")) a->audio_only = true;
        else if (!strcmp(argv[i], "--url") && i + 1 < argc) url = argv[++i];
        else if (!strcmp(argv[i], "--resume")) do_resume = true;
        else if (!strcmp(argv[i], "--restarted")) {}  // (by gs_update_restart_as; gs_update_restarted reads it)
        else if (!strcmp(argv[i], "--sign-in")) do_sign_in = true;
        else if (!strcmp(argv[i], "--sign-out")) do_sign_out = true;
        else if (!strcmp(argv[i], "--api") && i + 1 < argc) {
            api_kind = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') api_arg = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') api_quality = argv[++i];
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) a->shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) a->shot_at = SDL_atof(argv[++i]);
        else if (!strcmp(argv[i], "--stats") && i + 1 < argc) a->stats_file = argv[++i], a->show_stats = true;
        else if (!strcmp(argv[i], "--quit") && i + 1 < argc) a->quit_at = SDL_atof(argv[++i]);
        else return fprintf(stderr, "unknown option %s\n%s", argv[i], usage), SDL_APP_FAILURE;
    }
    data_dir(data, a->dir, sizeof a->dir);
    if (do_sign_out) return twitch_session_erase(a->dir), printf("signed out\n"), SDL_APP_SUCCESS;
    if (do_sign_in) return sign_in_here(a->dir) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    if (api_kind) return api(a->dir, api_kind, api_arg, api_quality, json) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

    // A shot is drawn in memory by the software renderer; otherwise the video path picks the renderer.
    const char *driver = NULL;
    if (a->shot) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"), SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    else driver = gs_video_prepare(!a->software);
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");  // (the icon built into the Windows program)
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | (a->shot ? 0 : SDL_INIT_AUDIO))) return SDL_Log("SDL_Init: %s", SDL_GetError()), SDL_APP_FAILURE;
    wake_event = SDL_RegisterEvents(1);
    a->persist = !a->shot && !a->demo && !a->stats_file;
    SDL_PropertiesID p = SDL_CreateProperties();
    // A test build says so, with its name: it replaces itself with every commit published.
#ifdef GSTREAM_BUILD
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "gstream (test build " GSTREAM_BUILD ")");
#else
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "gstream");
#endif
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1100);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 700);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, a->shot != NULL);
    a->win = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    if (!a->win || !(a->ren = SDL_CreateRenderer(a->win, a->shot ? NULL : driver))) return SDL_Log("window: %s", SDL_GetError()), SDL_APP_FAILURE;
    // gs_ui puts the window back where it was left, from window.txt in the data folder, and keeps it there.
    char place[1200];
    snprintf(place, sizeof place, "%swindow.txt", a->dir);
    SDL_SetStringProperty(SDL_GetWindowProperties(a->win), GS_UI_PLACE, a->persist ? place : "");
    a->fonts = gs_fontset_system();
    a->ui = gs_ui_new(a->win, a->ren, a->fonts);
    SDL_ShowWindow(a->win);
    gs_pace_set(&a->pace, a->win, a->ren, true, GS_PACE_DISPLAY);
    a->jobs = gs_jobs_new(4);
    if (!a->shot && !gs_mix_open(48000)) SDL_Log("audio: %s", SDL_GetError());  // (plays on without sound)
    a->volume = 0.8f;
    if (a->persist) load_volume(a);
    player_init(&a->player, a->ren, wake);
    player_set_volume(&a->player, a->volume, a->muted);
    if (a->persist) a->player.bandwidth = load_bandwidth(a);
    a->images = gs_images_new(a->ren, a->jobs, 8u << 20);
    a->lock = SDL_CreateMutex();
    live_init(&a->live);
    a->selected = 0;
    if (a->demo) {
        live_demo(&a->live, a->images);
        a->auth = SIGNED_IN;
    } else if (twitch_session_load(a->dir, &a->session)) {
        a->auth = SIGNED_IN;
        gs_jobs_add(a->jobs, restore_job, a);
    }
    if (play || url) {  // straight to a channel or a playlist, signed in or not: playback needs no account
        a->auth = a->auth == SIGNED_OUT && !a->demo ? SIGNED_IN : a->auth;
        if (url) player_play_url(&a->player, url, !a->audio_only);
        else watch(a, play, play);
        a->theater = true;
    } else if (do_resume && a->persist) resume(a);
    // Test modes draw every frame; otherwise the window waits for events, timers and finished work.
    if (!a->shot && !a->stats_file) SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "waitevent");
    if (a->quit_at > 0) SDL_AddTimer((Uint32)(a->quit_at * 1000), quit_later, NULL);  // (a waiting window may not draw again)
    // A published build on the user's own data keeps itself up to date (GSTREAM_UPDATE_FROM, for
    // trying it out, allows any data).
    // It looks only once its user has said yes, which views.c asks on the first run.
    if (a->persist && (!data || SDL_getenv("GSTREAM_UPDATE_FROM"))) a->update = update_start(a);
#ifdef GSTREAM_BUILD
    if (gs_update_rolled_back(a->update)) say(a, "Build " GSTREAM_BUILD " kept failing, so gstream goes back to the build before it.");
    else if (restarted) say(a, "Updated to build " GSTREAM_BUILD ".");
#endif
    (void)restarted;
    a->started = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *e) {
    app *a = state;
    if (e->type == SDL_EVENT_QUIT || e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_MOUSE_MOTION) a->pointer_moved = SDL_GetTicks();
    if (e->type == SDL_EVENT_MOUSE_MOTION || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_WHEEL || e->type == SDL_EVENT_KEY_DOWN) a->input_at = SDL_GetTicks();
    if (e->type == SDL_EVENT_KEY_DOWN && (e->key.key == SDLK_F11 || (e->key.key == SDLK_RETURN && (e->key.mod & SDL_KMOD_ALT)))) {
        set_full_screen(a, !(SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN));
        return SDL_APP_CONTINUE;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_F1) a->show_stats = !a->show_stats;
    gs_ui_event(a->ui, e);
    return SDL_APP_CONTINUE;
}

// Whether gstream is in the middle of something an update's restart would break: a channel playing,
// or being looked for.
static bool busy(app *a) {
    SDL_LockMutex(a->player.lock);
    bool playing = a->player.state != PLAYER_IDLE;
    SDL_UnlockMutex(a->player.lock);
    return playing;
}

// When the window should next draw on its own: every frame while video plays; otherwise for the
// interface's timers, images arriving, the player's status, and the list's refresh.
static void schedule(app *a) {
    if (a->shot || a->stats_file) return;
    static bool continuous;
    SDL_LockMutex(a->player.lock);
    bool video = a->player.live && a->player.video, playing = a->player.state != PLAYER_IDLE;
    SDL_UnlockMutex(a->player.lock);
    if (video != continuous) SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, (continuous = video) ? "0" : "waitevent");
    if (continuous) return;
    int ms = gs_ui_wait_ms(a->ui);
    if (playing && (ms < 0 || ms > 500)) ms = 500;  // the player's status and buffer
    if (a->theater && SDL_GetTicks() - a->pointer_moved < 3100 && (ms < 0 || ms > 200)) ms = 200;  // to hide the controls
    if (gs_jobs_pending(a->jobs) && (ms < 0 || ms > 150)) ms = 150;  // downloads and decodes finishing
    SDL_LockMutex(a->live.lock);
    uint64_t next = a->live.next_refresh;
    SDL_UnlockMutex(a->live.lock);
    if (a->auth == SIGNED_IN && !a->live.demo) {
        uint64_t now = SDL_GetTicks();
        int until = next > now ? (int)(next - now) : 0;
        if (ms < 0 || until < ms) ms = until + 1;
    }
    if (gs_update_ready(a->update) && !playing) {  // (to ask gs_update_due once the window has been left alone, then every 30 seconds)
        int until = (int)((int64_t)(a->input_at + 120000) - (int64_t)SDL_GetTicks());
        until = until > 0 ? until : 30000;
        if (ms < 0 || until < ms) ms = until + 1;
    }
    if (a->notice_until > SDL_GetTicks()) {  // (to take the notice away)
        int until = (int)(a->notice_until - SDL_GetTicks());
        if (ms < 0 || until < ms) ms = until + 1;
    }
    if (wake_timer) SDL_RemoveTimer(wake_timer), wake_timer = 0;
    if (ms >= 0) wake_timer = SDL_AddTimer((Uint32)(ms ? ms : 1), wake_later, NULL);
}

SDL_AppResult SDL_AppIterate(void *state) {
    app *a = state;
    // A newer build is in place: once nothing plays and the window has been left alone for two minutes
    // (and the last restart was 15 minutes ago), or when the user asks, gstream quits as usual, saving
    // what it showed, and the new build starts and goes on with it.
    if (a->restart || gs_update_due(a->update, busy(a), a->input_at)) return a->restart = true, SDL_APP_SUCCESS;
    gs_stats_frame_begin(&a->stats);
    if (a->auth == SIGNED_IN && a->session.access[0]) live_poll(&a->live, a->jobs, GSTREAM_CLIENT_ID, &a->session, a->lock, a->dir, wake);
    player_poll(&a->player, a->jobs);
    views_draw(a);
    gs_ui_end(a->ui);
    gs_images_end_frame(a->images);
    gs_stats_frame_end(&a->stats);
    if (a->show_stats) {
        char note[640];
        gs_pace_describe(&a->pace, note, sizeof note);
        size_t n = strlen(note);
        SDL_snprintf(note + n, sizeof note - n, "\nimages %.1f MB cached", gs_images_bytes(a->images) / 1048576.0);
        SDL_LockMutex(a->player.lock);
        if (!a->player.live && a->player.state != PLAYER_IDLE) {
            static const char *const pstates[] = { "idle", "resolving", "playing", "failed" };
            n = strlen(note);
            SDL_snprintf(note + n, sizeof note - n, "\nplayer %s %s %s", a->player.login, pstates[a->player.state], a->player.message);
        }
        if (a->player.live) {
            static const char *const states[] = { "starting", "playing", "buffering", "ended", "failed" };
            gs_live_info i = gs_live_get_info(a->player.live);
            n = strlen(note);
            SDL_snprintf(note + n, sizeof note - n, "\nstream %s %s, %s%s\nclock %.2f s, buffer %.2f s, queued %.1f s, behind %.1f s\nsegments %d, discontinuities %d, skips %d, stalls %d, renewals %d",
                         a->player.login, a->player.playing, states[i.state], a->player.ad ? ", ad" : "", i.clock, i.buffered, i.queued, i.behind,
                         i.segments, i.discontinuities, i.skips, i.stalls, i.renewals);
            gs_hls_variant v;
            if (gs_live_rendition(a->player.live, &v)) {
                n = strlen(note);
                SDL_snprintf(note + n, sizeof note - n, "\nadaptive %dx%d at %.2f Mbit/s, estimate %.2f Mbit/s, switches %d", v.width, v.height,
                             v.bandwidth / 1e6, i.bandwidth / 1e6, i.switches);
            }
            if (a->player.video) {
                n = strlen(note);
                SDL_snprintf(note + n, sizeof note - n, "\nvideo %s %dx%d, shown %lld, dropped %lld, queued %d\nclock error %.1f ms (largest %.1f), jitter %.1f ms",
                             i.video.path ? i.video.path : "-", i.video.width, i.video.height, i.video.shown, i.video.dropped, i.video.queued,
                             i.video.error_ms, i.video.error_max_ms, i.video.jitter_ms);
            }
        }
        SDL_UnlockMutex(a->player.lock);
        gs_stats_draw(&a->stats, a->ren, -12, 52, note);
        uint64_t now = SDL_GetTicks();
        if (a->stats_file && now >= a->stats_next) {
            a->stats_next = now + 1000;
            FILE *out = fopen(a->stats_file, "a");
            if (out) {
                fprintf(out, "%.1f s: %.0f fps, frame %.2f ms (max %.2f), cpu %.1f%%, memory %.1f MB\n%s\n\n", (now - a->started) / 1000.0, a->stats.fps,
                        a->stats.frame_ms, a->stats.frame_max_ms, a->stats.cpu_percent, a->stats.ram_bytes / 1048576.0, note);
                fclose(out);
            }
        }
    }
    uint64_t ran = SDL_GetTicks() - a->started;
    if (a->quit_at > 0 && ran >= a->quit_at * 1000) return SDL_APP_SUCCESS;
    // (a glyph first drawn in one frame appears from the next, so a shot waits a few frames)
    if (a->shot && ran >= a->shot_at * 1000 && ++a->frames > 3 && !gs_jobs_pending(a->jobs)) {
        SDL_Surface *s = SDL_RenderReadPixels(a->ren, NULL), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : NULL;
        bool ok = c && stbi_write_png(a->shot, c->w, c->h, 4, c->pixels, c->pitch);
        SDL_DestroySurface(c), SDL_DestroySurface(s);
        if (ok) {  // for scripts: when the stream says the frame in the shot was made, and when it was captured
            SDL_Time now_ns = 0;
            SDL_GetCurrentTime(&now_ns);
            SDL_LockMutex(a->player.lock);
            gs_live_info i = a->player.live ? gs_live_get_info(a->player.live) : (gs_live_info){ 0 };
            SDL_UnlockMutex(a->player.lock);
            double captured = now_ns / 1e9;
            char at[40], made[40];
            utc_text(captured, at, sizeof at);
            printf("{\"shot\":"), print_json(a->shot), printf(",\"captured\":%.3f,\"captured_utc\":\"%s\"", captured, at);
            if (i.date > 0) utc_text(i.date, made, sizeof made), printf(",\"frame_time\":%.3f,\"frame_utc\":\"%s\",\"behind\":%.3f", i.date, made, captured - i.date);
            else printf(",\"frame_time\":null");
            printf(",\"width\":%d,\"height\":%d}\n", i.video.width, i.video.height);
        }
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    SDL_RenderPresent(a->ren);
    gs_pace_wait(&a->pace);
    schedule(a);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *state, SDL_AppResult result) {
    app *a = state;
    (void)result;
    if (!a) return;
    if (wake_timer) SDL_RemoveTimer(wake_timer);
    SDL_SetAtomicInt(&a->auth_cancel, 1);
    if (a->jobs) gs_jobs_wait(a->jobs);
    if (a->persist && a->player.lock) save_resume(a), save_volume(a);
    if (a->player.lock) player_free(&a->player);  // (which records the last estimate)
    if (a->persist && a->player.bandwidth > 0) save_bandwidth(a, a->player.bandwidth);
    gs_mix_close();
    gs_jobs_free(a->jobs);
    gs_images_free(a->images);
    live_free(&a->live);
    gs_ui_free(a->ui);
    gs_fontset_free(a->fonts);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_DestroyMutex(a->lock);
    if (a->restart) {  // (on with what this one saved as it quit; gs_update_restart_as adds --restarted)
        char *args[] = { PROGRAM, "--resume", "--data", (char *)a->data, NULL };
        if (!gs_update_restart_as(a->update, a->data ? 4 : 2, args)) SDL_Log("gstream: the new build could not be started: %s", SDL_GetError());
    }
    gs_update_free(a->update);  // (a run that ends without it counts against a build on trial)
    SDL_free(a);
}
