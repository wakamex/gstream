#include "app.h"

#include <stdio.h>
#include <string.h>

static SDL_FColor alpha(SDL_FColor c, float a) { return (SDL_FColor){ c.r, c.g, c.b, a }; }

// "12.3K", "1.2M": viewer counts as Twitch shows them.
static void count_text(int n, char *out, size_t size) {
    if (n >= 1000000) SDL_snprintf(out, size, "%.1fM", n / 1e6);
    else if (n >= 1000) SDL_snprintf(out, size, "%.1fK", n / 1e3);
    else SDL_snprintf(out, size, "%d", n);
}

// ---- Signed out: one card in the middle of the window ----

static void draw_sign_in(app *a, gs_rect win) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    gs_rect card = { win.x + (win.w - 420) / 2, win.y + (win.h - 300) / 2, 420, 300 };
    gs_ui_fill(ui, card, st->panel);
    gs_ui_frame(ui, card, st->border);
    gs_rect c = gs_inset(card, 28);
    gs_ui_text(ui, gs_cut_top(&c, 40), "gstream", 28, st->accent, 0);
    gs_ui_text(ui, gs_cut_top(&c, 26), "Watch the Twitch channels you follow", st->text_px, st->muted, 0);
    gs_cut_top(&c, 24);
    SDL_LockMutex(a->lock);
    auth_state auth = a->auth;
    gs_oauth_device d = a->device;
    char message[200];
    SDL_strlcpy(message, a->auth_message, sizeof message);
    bool busy = a->auth_busy;
    SDL_UnlockMutex(a->lock);
    if (auth == SIGNING_IN && d.user_code[0]) {
        gs_ui_text(ui, gs_cut_top(&c, 22), "Enter this code on Twitch's page:", st->text_px, st->text, 0);
        gs_ui_text(ui, gs_cut_top(&c, 52), d.user_code, 34, st->text, 0);
        gs_rect buttons = gs_cut_top(&c, st->row);
        gs_rect open = gs_cut_left(&buttons, (buttons.w - 12) / 2);
        gs_cut_left(&buttons, 12);
        if (gs_ui_button(ui, open, "Open page")) SDL_OpenURL(d.verification_uri);
        if (gs_ui_button(ui, buttons, "Cancel") || gs_ui_key(ui, SDLK_ESCAPE, 0)) sign_in_cancel(a);
    } else {
        gs_rect button = gs_cut_top(&c, st->row + 6);
        button = (gs_rect){ button.x + 60, button.y, button.w - 120, button.h };
        bool ready = GSTREAM_CLIENT_ID[0] != 0 && !busy;
        gs_ui_fill(ui, button, ready && gs_ui_hovered(ui, button) ? alpha(st->accent, 0.85f) : ready ? st->accent : st->raised);
        gs_ui_text(ui, button, busy ? "Starting\xE2\x80\xA6" : "Sign in with Twitch", st->text_px, ready ? st->accent_text : st->muted, 0);
        if (ready && (gs_ui_clicked(ui, button) || gs_ui_key(ui, SDLK_RETURN, 0))) sign_in_start(a);
    }
    gs_cut_top(&c, 14);
    const char *note = !GSTREAM_CLIENT_ID[0] ? "This build has no Twitch Client ID (TWITCH_CLIENT_ID in .env when building)." : message;
    gs_ui_text(ui, gs_cut_top(&c, 20), note, st->small_px, st->muted, 0);
}

// ---- The sidebar: filters and the live channels ----

static const char *const sort_names[] = { "Viewers", "Channel", "Category", "Started" };

static void draw_card(app *a, gs_rect r, const twitch_stream *s, bool selected) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    if (selected) gs_ui_fill(ui, r, st->raised);
    else if (gs_ui_hovered(ui, r)) gs_ui_fill(ui, r, alpha(st->raised, 0.6f));
    gs_rect in = gs_inset(r, 6);
    char viewers[16];
    count_text(s->viewers, viewers, sizeof viewers);
    bool playing = a->player.state != PLAYER_IDLE && !strcmp(a->player.login, s->login);
    if (a->compact) {
        gs_rect v = gs_cut_right(&in, 56);
        gs_ui_text(ui, v, viewers, st->small_px, st->muted, 1);
        gs_rect name = gs_cut_left(&in, in.w * 0.45f);
        gs_ui_text(ui, name, s->name, st->text_px, playing ? st->accent : st->text, -1);
        gs_ui_text(ui, in, s->category, st->small_px, st->muted, -1);
        return;
    }
    gs_rect thumb = gs_cut_left(&in, (in.h) * 16 / 9);
    char url[600];
    twitch_thumbnail_url(s->thumbnail, 320, 180, url, sizeof url);
    float px = gs_ui_scale(ui);
    SDL_Texture *t = gs_images_get(a->images, url, (int)(thumb.w * px + 0.5f), (int)(thumb.h * px + 0.5f));
    if (t) gs_ui_texture(ui, thumb, t, NULL);
    else gs_ui_fill(ui, thumb, st->raised);
    gs_cut_left(&in, 10);
    gs_rect top = gs_cut_top(&in, in.h / 3);
    gs_rect v = gs_cut_right(&top, 56);
    gs_ui_text(ui, v, viewers, st->small_px, st->warning, 1);
    gs_ui_text(ui, top, s->name, st->text_px, playing ? st->accent : st->text, -1);
    gs_ui_text(ui, gs_cut_top(&in, in.h / 2), s->title, st->small_px, st->text, -1);
    gs_ui_text(ui, in, s->category, st->small_px, st->muted, -1);
}

static void draw_sidebar(app *a, gs_rect side) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    live *l = &a->live;
    gs_ui_fill(ui, side, st->panel);
    gs_rect c = gs_inset(side, 14);
    gs_ui_text(ui, gs_cut_top(&c, 34), "gstream", 22, st->text, -1);
    gs_cut_top(&c, 6);
    if (gs_ui_field(ui, gs_cut_top(&c, st->row), "filter", l->filter, sizeof l->filter, "Search follows")) l->view_version = -1;
    gs_cut_top(&c, 8);
    gs_rect row = gs_cut_top(&c, st->row);
    gs_rect density = gs_cut_right(&row, 54);
    if (gs_ui_button(ui, density, a->compact ? "Cards" : "Rows")) a->compact = !a->compact;
    gs_ui_tooltip(ui, density, a->compact ? "Switch to cards" : "Switch to compact rows");
    gs_cut_right(&row, 6);
    char count[32];
    SDL_LockMutex(l->lock);
    SDL_snprintf(count, sizeof count, "%d/%d", l->shown, l->count);
    bool stale = l->stale, loaded = l->loaded;
    char updated[16], error[160];
    SDL_strlcpy(updated, l->updated_at, sizeof updated);
    SDL_strlcpy(error, l->error, sizeof error);
    SDL_UnlockMutex(l->lock);
    gs_ui_text(ui, gs_cut_right(&row, 50), count, st->small_px, st->muted, 1);
    gs_cut_right(&row, 6);
    if (gs_ui_toggle(ui, gs_cut_right(&row, 52), "18+", &l->mature)) l->view_version = -1;
    gs_cut_right(&row, 8);
    if (gs_ui_dropdown(ui, row, "sort", sort_names, 4, &a->sort)) l->sort = (live_sort)a->sort, l->view_version = -1;
    gs_cut_top(&c, 8);
    if (stale || (!loaded && error[0])) {
        char warn[200];
        SDL_snprintf(warn, sizeof warn, updated[0] ? "Couldn't refresh, last update %s" : "Couldn't load your follows: %s", updated[0] ? updated : error);
        gs_ui_text(ui, gs_cut_top(&c, 22), warn, st->small_px, st->warning, -1);
    }
    if (l->view_version != l->version) live_update_view(l);
    if (!l->shown) {
        const char *empty = !loaded ? "Loading your follows\xE2\x80\xA6" : l->count ? "No channels match the filters" : "No followed channels are live right now";
        gs_ui_text(ui, gs_cut_top(&c, 24), empty, st->text_px, st->muted, -1);
        return;
    }
    float row_h = a->compact ? 34 : 76;
    gs_ui_rows rows = gs_ui_list(ui, c, "channels", l->shown, row_h, &a->selected);
    for (int i = rows.first; i < rows.end; i++) {
        const twitch_stream *s = &l->all[l->view[i]];
        draw_card(a, (gs_rect){ c.x, rows.y + i * row_h, c.w, row_h - 2 }, s, i == a->selected);
    }
    gs_ui_list_end(ui);
    if (rows.activated && a->selected >= 0 && a->selected < l->shown) {
        const twitch_stream *s = &l->all[l->view[a->selected]];
        if (a->live.demo) SDL_strlcpy(a->player.login, s->login, sizeof a->player.login);  // (made-up channels cannot play)
        else watch(a, s->login, s->name);
    }
}

// ---- The player ----

// The player alone, full screen, or back to the window as it was.
static void set_theater(app *a, bool on) {
    a->theater = on;
    SDL_SetWindowFullscreen(a->win, on);
}

static void percent(float v, char *out, size_t size) { SDL_snprintf(out, size, "%d%%", (int)(v * 100 + 0.5f)); }

// The picture, fitted to the stage with its own aspect, or what the player is doing instead.
static void draw_stage(app *a, gs_rect stage) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    gs_ui_fill(ui, stage, (SDL_FColor){ 0, 0, 0, 1 });
    player *p = &a->player;
    SDL_LockMutex(p->lock);
    player_state state = p->state;
    gs_live *live = p->live;
    char name[128], message[200];
    SDL_strlcpy(name, p->name, sizeof name), SDL_strlcpy(message, p->message, sizeof message);
    bool ad = p->ad, video = p->video;
    SDL_UnlockMutex(p->lock);
    SDL_FRect src;
    SDL_Texture *t = live ? gs_live_frame(live, &src) : NULL;
    if (t && src.w > 0 && src.h > 0) {
        float k = SDL_min(stage.w / src.w, stage.h / src.h);
        gs_rect fit = { stage.x + (stage.w - src.w * k) / 2, stage.y + (stage.h - src.h * k) / 2, src.w * k, src.h * k };
        gs_ui_texture(ui, fit, t, &src);
    }
    gs_live_info info = live ? gs_live_get_info(live) : (gs_live_info){ 0 };
    const char *status = NULL;
    if (state == PLAYER_IDLE) status = "Pick a channel to watch";
    else if (state == PLAYER_RESOLVING) status = "Finding the stream\xE2\x80\xA6";
    else if (state == PLAYER_FAILED) status = message;
    else if (info.state == GS_LIVE_FAILED || info.state == GS_LIVE_ENDED) status = info.message[0] ? info.message : "The stream has ended";
    else if (info.state == GS_LIVE_STARTING) status = "Starting\xE2\x80\xA6";
    else if (info.state == GS_LIVE_BUFFERING) status = "Buffering\xE2\x80\xA6";
    else if (!t && video) status = "Waiting for the picture\xE2\x80\xA6";
    if (status) {
        gs_rect mid = { stage.x, stage.y + stage.h / 2 - 40, stage.w, 80 };
        if (state != PLAYER_IDLE) gs_ui_text(ui, gs_cut_top(&mid, 40), name, 22, st->text, 0);
        gs_ui_text(ui, gs_cut_top(&mid, 40), status, st->text_px, st->muted, 0);
        bool failed = state == PLAYER_FAILED || info.state == GS_LIVE_FAILED || info.state == GS_LIVE_ENDED;
        if (failed && SDL_strcmp(p->login, "url") && gs_ui_button(ui, (gs_rect){ mid.x + (mid.w - 120) / 2, mid.y + 8, 120, st->row }, "Try again"))
            player_retry(p, a->jobs);
    }
    if (ad && state == PLAYER_PLAYING) {
        gs_rect badge = { stage.x + 12, stage.y + 12, 44, 22 };
        gs_ui_fill(ui, badge, st->warning);
        gs_ui_text(ui, badge, "Ad", st->small_px, st->accent_text, 0);
    }
}

static void draw_player_bar(app *a, gs_rect bar) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    player *p = &a->player;
    gs_ui_fill(ui, bar, st->panel);
    gs_rect c = gs_inset(bar, 6);
    SDL_LockMutex(p->lock);
    char label[200];
    if (p->state == PLAYER_IDLE) label[0] = 0;
    else SDL_snprintf(label, sizeof label, "%s%s%s", p->name, p->playing[0] ? " \xC2\xB7 " : "", p->playing);
    player_state state = p->state;
    // The quality: "Best", then the channel's renditions once it has resolved.
    char names[16][32];
    const char *items[17] = { "Best" };
    int count = 1, chosen = 0;
    for (int i = 0; i < p->quality_count; i++) {
        if (!SDL_strcmp(p->qualities[i], p->quality)) chosen = count;
        items[count++] = SDL_memcpy(names[i], p->qualities[i], sizeof names[i]);
    }
    SDL_UnlockMutex(p->lock);
    gs_rect full = gs_cut_right(&c, 70);
    if (gs_ui_button(ui, full, a->theater ? "Window" : "Full") || gs_ui_key(ui, SDLK_F, 0)) set_theater(a, !a->theater);
    gs_cut_right(&c, 8);
    char pct[16];
    percent(a->volume, pct, sizeof pct);
    gs_ui_text(ui, gs_cut_right(&c, 44), pct, st->small_px, st->muted, 1);
    gs_cut_right(&c, 6);
    float v = a->volume;
    if (gs_ui_slider(ui, gs_cut_right(&c, 110), "volume", &v, 0, 1)) a->volume = v, player_set_volume(p, a->volume, a->muted);
    gs_cut_right(&c, 8);
    if (gs_ui_button(ui, gs_cut_right(&c, 70), a->muted ? "Unmute" : "Mute") || gs_ui_key(ui, SDLK_M, 0)) a->muted = !a->muted, player_set_volume(p, a->volume, a->muted);
    gs_cut_right(&c, 6);
    if (state != PLAYER_IDLE && gs_ui_button(ui, gs_cut_right(&c, 64), "Stop")) player_stop(p), set_theater(a, false);
    gs_cut_right(&c, 6);
    if (count > 1 && gs_ui_dropdown(ui, gs_cut_right(&c, 118), "quality", items, count, &chosen)) {
        char login[64], name[128];
        SDL_strlcpy(login, p->login, sizeof login), SDL_strlcpy(name, p->name, sizeof name);
        player_start(p, a->jobs, login, name, chosen ? items[chosen] : "best", !a->audio_only);
    }
    gs_cut_right(&c, 10);
    gs_ui_text(ui, c, label, st->text_px, st->text, -1);
}

// ---- The main area and the account menu ----

static void draw_main(app *a, gs_rect m) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    gs_rect top = gs_cut_top(&m, 44);
    gs_rect account = gs_cut_right(&top, 150);
    account = gs_inset(account, 6);
    char label[96];
    SDL_LockMutex(a->lock);
    SDL_snprintf(label, sizeof label, "%s", a->demo ? "demo" : a->session.login);
    SDL_UnlockMutex(a->lock);
    if (gs_ui_button(ui, account, label)) gs_ui_menu_toggle(ui, "account");
    gs_ui_arrow(ui, (gs_rect){ account.x + account.w - 18, account.y, 12, account.h }, st->muted);
    gs_rect bar = gs_cut_bottom(&m, 44);
    draw_stage(a, gs_inset(m, 14));
    draw_player_bar(a, bar);
    if (gs_ui_menu_begin(ui, "account", account, 200)) {
        char who[96];
        SDL_snprintf(who, sizeof who, "Signed in as %s", a->demo ? "demo" : a->session.login);
        gs_ui_menu_item(ui, who, false);
        if (gs_ui_menu_item(ui, a->show_stats ? "Hide diagnostics (F1)" : "Diagnostics (F1)", true)) a->show_stats = !a->show_stats;
        if (gs_ui_menu_item(ui, "About gstream", true)) a->about = true;
        if (gs_ui_menu_item(ui, "Sign out", !a->demo)) sign_out(a);
        gs_ui_menu_end(ui);
    }
}

// The player filling the window; its controls show while the pointer has moved in the last 3 seconds.
static void draw_theater(app *a, gs_rect win) {
    draw_stage(a, win);
    if (SDL_GetTicks() - a->pointer_moved < 3000) draw_player_bar(a, gs_cut_bottom(&win, 44));
    if (gs_ui_key(a->ui, SDLK_ESCAPE, 0)) set_theater(a, false);
}

// What gstream is built from, with each part's licence.
static void draw_about(app *a, gs_rect win) {
    gs_ui *ui = a->ui;
    const gs_ui_style *st = gs_ui_style_of(ui);
    gs_rect card = { win.x + (win.w - 480) / 2, win.y + (win.h - 360) / 2, 480, 360 };
    gs_ui_fill(ui, card, st->panel);
    gs_ui_frame(ui, card, st->border);
    gs_rect c = gs_inset(card, 24);
    char line[200];
    gs_ui_text(ui, gs_cut_top(&c, 36), "gstream " GSTREAM_VERSION, 24, st->text, -1);
    gs_ui_text(ui, gs_cut_top(&c, 22), "Watch the Twitch channels you follow. Not made or endorsed by Twitch.", st->small_px, st->muted, -1);
    gs_cut_top(&c, 12);
    int sdl = SDL_GetVersion();
    char sdl_line[96];
    SDL_snprintf(sdl_line, sizeof sdl_line, "SDL %d.%d.%d, zlib licence", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl));
    SDL_snprintf(line, sizeof line, "FFmpeg %s (libavcodec), LGPL 2.1 or later", gs_video_library());
    const char *parts[] = { "Built from:", "gesso, MIT licence", sdl_line, line, "stb_image and stb_truetype, public domain", "kb_text_shape, zlib licence" };
    for (size_t i = 0; i < SDL_arraysize(parts); i++) gs_ui_text(ui, gs_cut_top(&c, 22), parts[i], st->small_px, i ? st->text : st->muted, -1);
    gs_cut_top(&c, 8);
    gs_ui_text(ui, gs_cut_top(&c, 20), "The licences are in the licenses folder beside the program.", st->small_px, st->muted, -1);
    gs_ui_text(ui, gs_cut_top(&c, 20), "FFmpeg's source is attached to each release.", st->small_px, st->muted, -1);
    gs_rect close = gs_cut_bottom(&c, st->row);
    close = gs_cut_right(&close, 90);
    if (gs_ui_button(ui, close, "Close") || gs_ui_key(ui, SDLK_ESCAPE, 0) || gs_ui_key(ui, SDLK_RETURN, 0)) a->about = false;
}

void views_draw(app *a) {
    gs_ui *ui = a->ui;
    gs_rect win = gs_ui_begin(ui);
    gs_ui_fill(ui, win, gs_ui_style_of(ui)->background);
    SDL_LockMutex(a->lock);
    auth_state auth = a->auth;
    SDL_UnlockMutex(a->lock);
    if (a->about) {
        draw_about(a, win);  // (in place of the rest, which would otherwise take its clicks)
    } else if (auth != SIGNED_IN) {
        draw_sign_in(a, win);
    } else if (a->theater) {
        draw_theater(a, win);
    } else {
        gs_rect side = gs_cut_left(&win, SDL_clamp(win.w * 0.32f, 280, 400));
        draw_sidebar(a, side);
        draw_main(a, win);
        // Keys for the list and the filter: / or Ctrl+F to search, Esc back to the list.
        if (gs_ui_key(ui, SDLK_SLASH, 0) || gs_ui_key(ui, SDLK_F, SDL_KMOD_CTRL)) gs_ui_focus(ui, "filter");
        if (gs_ui_focused(ui, "filter") && (gs_ui_key(ui, SDLK_ESCAPE, 0) || gs_ui_key(ui, SDLK_DOWN, 0) || gs_ui_key(ui, SDLK_RETURN, 0))) gs_ui_focus(ui, "channels");
        if (!gs_ui_focused(ui, "filter") && !gs_ui_focused(ui, "channels")) gs_ui_focus(ui, "channels");
    }
}
