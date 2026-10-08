// streamit: a native Twitch client on gesso. Sign in, see who you follow is live, watch.
// Its options are in `usage` below. F1 shows the performance overlay; closing the window quits.
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

#include "gs_pace.h"
#include "gs_stats.h"
#include "gs_text.h"
#include "gs_video.h"
#include "stb_image_write.h"
#include "twitch.h"

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    gs_glyphs *glyphs;
    gs_fontset *fonts;
    gs_pace pace;
    gs_stats stats;
    bool show_stats;
    char dir[1024];       // the data folder, with a trailing separator
    const char *shot;     // --shot: render a frame to this PNG and quit
    double shot_at;
    const char *stats_file;
    double quit_at;
    uint64_t started, stats_next;
    int frames;
} app;

static const char usage[] =
    "streamit                                  sign in and watch the channels you follow\n"
    "  --sign-in                               sign in here in the terminal, then quit\n"
    "  --sign-out                              forget the saved session\n"
    "  --api live                              print the followed channels that are live\n"
    "  --api resolve CHANNEL [QUALITY]         print a channel's stream (QUALITY such as best, 720p60, audio_only)\n"
    "  --software                              decode video in software\n"
    "  --data DIR                              where the session and settings live\n"
    "  --shot F.png [--at S]                   render one frame at S seconds, headless, and quit\n"
    "  --stats FILE [--quit S]                 add the performance overlay's text to FILE each second;\n"
    "                                          quit after S seconds\n";

// --sign-in: the device code flow in the terminal.
static bool sign_in_here(const char *dir) {
    gs_oauth_client c = twitch_oauth_client(STREAMIT_CLIENT_ID);
    gs_oauth_device d = { 0 };
    char msg[256];
    if (!STREAMIT_CLIENT_ID[0]) return printf("no Twitch Client ID was built in (TWITCH_CLIENT_ID in .env)\n"), false;
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
    twitch_error e = twitch_session_from(STREAMIT_CLIENT_ID, &t, &s, msg, sizeof msg);
    SDL_memset(&t, 0, sizeof t);
    if (e != TW_OK || !twitch_session_save(dir, &s)) return printf("not signed in: %s\n", e != TW_OK ? msg : "the session could not be saved"), false;
    printf("signed in as %s\n", s.login);
    return true;
}

// --api: one answer from Twitch, printed.
static bool api(const char *dir, const char *kind, const char *arg, const char *quality) {
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
    if (!strcmp(kind, "live")) {
        twitch_session s;
        if (!twitch_session_load(dir, &s)) return printf("not signed in (streamit --sign-in)\n"), false;
        static twitch_stream streams[500];
        int n;
        char before[sizeof s.access];
        SDL_strlcpy(before, s.access, sizeof before);
        twitch_error e = twitch_followed_live(STREAMIT_CLIENT_ID, &s, streams, 500, &n, msg, sizeof msg);
        if (strcmp(before, s.access)) twitch_session_save(dir, &s);  // renewed on the way
        if (e != TW_OK) return printf("%s: %s\n", twitch_error_text(e), msg), false;
        for (int i = 0; i < n; i++)
            printf("%7d  %-20s %-28.28s %s\n", streams[i].viewers, streams[i].login, streams[i].category, streams[i].title);
        printf("%d live\n", n);
        return true;
    }
    return printf("%s", usage), false;
}

static void data_dir(const char *data, char *out, size_t size) {
    if (data) {
        size_t n = strlen(data);
        snprintf(out, size, "%s%s", data, n && (data[n - 1] == '/' || data[n - 1] == '\\') ? "" :
#ifdef _WIN32
                 "\\"
#else
                 "/"
#endif
        );
        SDL_CreateDirectory(data);
        return;
    }
    char *pref = SDL_GetPrefPath("wakamex", "streamit");  // (made if missing)
    snprintf(out, size, "%s", pref ? pref : "");
    SDL_free(pref);
}

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
    app *a = SDL_calloc(1, sizeof *a);
    *state = a;
    const char *data = NULL, *api_kind = NULL, *api_arg = NULL, *api_quality = NULL;
    bool software = false, sign_in = false, sign_out = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return printf("%s", usage), SDL_APP_SUCCESS;
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--software")) software = true;
        else if (!strcmp(argv[i], "--sign-in")) sign_in = true;
        else if (!strcmp(argv[i], "--sign-out")) sign_out = true;
        else if (!strcmp(argv[i], "--api") && i + 1 < argc) {
            api_kind = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') api_arg = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') api_quality = argv[++i];
        }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) a->shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) a->shot_at = SDL_atof(argv[++i]);
        else if (!strcmp(argv[i], "--stats") && i + 1 < argc) a->stats_file = argv[++i], a->show_stats = true;
        else if (!strcmp(argv[i], "--quit") && i + 1 < argc) a->quit_at = SDL_atof(argv[++i]);
        else return fprintf(stderr, "unknown option %s\n%s", argv[i], usage), SDL_APP_FAILURE;
    }
    data_dir(data, a->dir, sizeof a->dir);
    if (sign_out) return twitch_session_erase(a->dir), printf("signed out\n"), SDL_APP_SUCCESS;
    if (sign_in) return sign_in_here(a->dir) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    if (api_kind) return api(a->dir, api_kind, api_arg, api_quality) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

    // A shot is drawn in memory by the software renderer; otherwise the video path picks the renderer.
    const char *driver = NULL;
    if (a->shot) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"), SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    else driver = gs_video_prepare(!software);
    if (!SDL_Init(SDL_INIT_VIDEO)) return SDL_Log("SDL_Init: %s", SDL_GetError()), SDL_APP_FAILURE;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "streamit");
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1100);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 700);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, a->shot != NULL);
    a->win = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    if (!a->win || !(a->ren = SDL_CreateRenderer(a->win, a->shot ? NULL : driver))) return SDL_Log("window: %s", SDL_GetError()), SDL_APP_FAILURE;
    gs_pace_set(&a->pace, a->win, a->ren, true, GS_PACE_DISPLAY);
    a->glyphs = gs_glyphs_new(a->ren, 1024);
    a->fonts = gs_fontset_system();
    a->started = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *e) {
    app *a = state;
    if (e->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_F1) a->show_stats = !a->show_stats;
    return SDL_APP_CONTINUE;
}

static SDL_FColor rgb(int r, int g, int b) { return (SDL_FColor){ r / 255.0f, g / 255.0f, b / 255.0f, 1 }; }

SDL_AppResult SDL_AppIterate(void *state) {
    app *a = state;
    gs_stats_frame_begin(&a->stats);
    int ow, oh;
    SDL_GetCurrentRenderOutputSize(a->ren, &ow, &oh);
    float u = SDL_GetWindowDisplayScale(a->win);
    gs_glyphs_begin_frame(a->glyphs);
    SDL_SetRenderDrawColor(a->ren, 17, 19, 18, 255);
    SDL_RenderClear(a->ren);
    gs_fontset_draw(a->glyphs, a->fonts, 28 * u, 32 * u, 56 * u, "streamit", rgb(53, 194, 165));
    gs_fontset_draw(a->glyphs, a->fonts, 15 * u, 32 * u, 90 * u, "Twitch, natively", rgb(150, 156, 152));
    gs_stats_frame_end(&a->stats);
    if (a->show_stats) {
        char pacing[200];
        gs_pace_describe(&a->pace, pacing, sizeof pacing);
        gs_stats_draw(&a->stats, a->ren, -12, 12, pacing);
        uint64_t now = SDL_GetTicks();
        if (a->stats_file && now >= a->stats_next) {
            a->stats_next = now + 1000;
            FILE *out = fopen(a->stats_file, "a");
            if (out) {
                fprintf(out, "%.1f s: %.0f fps, frame %.2f ms (max %.2f), cpu %.1f%%, memory %.1f MB\n%s\n\n", (now - a->started) / 1000.0, a->stats.fps,
                        a->stats.frame_ms, a->stats.frame_max_ms, a->stats.cpu_percent, a->stats.ram_bytes / 1048576.0, pacing);
                fclose(out);
            }
        }
    }
    uint64_t ran = SDL_GetTicks() - a->started;
    if (a->quit_at > 0 && ran >= a->quit_at * 1000) return SDL_APP_SUCCESS;
    // (a glyph first drawn in one frame appears from the next, so a shot waits a few frames)
    if (a->shot && ran >= a->shot_at * 1000 && ++a->frames > 3) {
        SDL_Surface *s = SDL_RenderReadPixels(a->ren, NULL), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : NULL;
        bool ok = c && stbi_write_png(a->shot, c->w, c->h, 4, c->pixels, c->pitch);
        SDL_DestroySurface(c), SDL_DestroySurface(s);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    SDL_RenderPresent(a->ren);
    gs_pace_wait(&a->pace);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *state, SDL_AppResult result) {
    app *a = state;
    (void)result;
    if (!a) return;
    gs_fontset_free(a->fonts);
    gs_glyphs_free(a->glyphs);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    SDL_free(a);
}
