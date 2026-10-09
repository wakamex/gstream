#include "twitch.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "gs_http.h"
#include "gs_json.h"
#include "gs_secret.h"

// The Twitch web player's public client, which the playback token request has to come from.
#define WEB_CLIENT_ID "kimne78kx3ncx6brgo4mv6wki5h1ko"
#define TOKEN_QUERY_HASH "ed230aa1e33e07eebb8928504583da78a5173989fadfb1ac94be06a04f3cdbe9"
#define AGENT "Mozilla/5.0"

const char *twitch_error_text(twitch_error e) {
    static const char *const text[] = {
        "ok", "the channel is offline", "sign in again", "subscribers only", "not available in your region",
        "age-restricted", "Twitch is limiting requests; try again shortly", "Twitch refused this player",
        "no answer from Twitch", "Twitch returned an error",
    };
    return (unsigned)e < SDL_arraysize(text) ? text[e] : "?";
}

static void say(char *message, size_t size, const char *text) {
    if (size) SDL_strlcpy(message, text, size);
}

// ---- Logins ----

bool twitch_valid_login(const char *login) {
    size_t n = 0;
    for (; login[n]; n++)
        if (!SDL_isalnum((unsigned char)login[n]) && login[n] != '_') return false;
    return n >= 1 && n <= 25;
}

void twitch_normalize_login(const char *login, char *out, size_t size) {
    size_t i = 0;
    for (; login[i] && i + 1 < size; i++) out[i] = (char)SDL_tolower((unsigned char)login[i]);
    if (size) out[i] = 0;
}

// ---- The session ----

static void session_path(const char *dir, char *out, size_t size) { SDL_snprintf(out, size, "%ssession", dir); }

bool twitch_session_load(const char *dir, twitch_session *s) {
    char path[1200];
    session_path(dir, path, sizeof path);
    size_t len;
    char *text = gs_secret_load(path, &len);
    gs_json *doc = text ? gs_json_parse(text, len) : NULL;
    if (text) SDL_memset(text, 0, len), SDL_free(text);
    gs_jv root = gs_json_root(doc);
    memset(s, 0, sizeof *s);
    SDL_strlcpy(s->access, gs_json_str(gs_json_get(root, "access"), ""), sizeof s->access);
    SDL_strlcpy(s->refresh, gs_json_str(gs_json_get(root, "refresh"), ""), sizeof s->refresh);
    SDL_strlcpy(s->user_id, gs_json_str(gs_json_get(root, "user_id"), ""), sizeof s->user_id);
    SDL_strlcpy(s->login, gs_json_str(gs_json_get(root, "login"), ""), sizeof s->login);
    gs_json_free(doc);
    return s->access[0] != 0;
}

// Tokens, ids and logins are letters, digits and underscores, so they need no JSON escaping; anything
// else is refused rather than written.
static bool plain(const char *s) {
    for (; *s; s++)
        if (!SDL_isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    return true;
}

bool twitch_session_save(const char *dir, const twitch_session *s) {
    if (!plain(s->access) || !plain(s->refresh) || !plain(s->user_id) || !plain(s->login)) return false;
    char path[1200], text[1400];
    session_path(dir, path, sizeof path);
    int n = SDL_snprintf(text, sizeof text, "{\"access\":\"%s\",\"refresh\":\"%s\",\"user_id\":\"%s\",\"login\":\"%s\"}", s->access, s->refresh, s->user_id, s->login);
    bool ok = n > 0 && (size_t)n < sizeof text && gs_secret_save(path, text, (size_t)n);
    SDL_memset(text, 0, sizeof text);
    return ok;
}

void twitch_session_erase(const char *dir) {
    char path[1200];
    session_path(dir, path, sizeof path);
    gs_secret_erase(path);
}

gs_oauth_client twitch_oauth_client(const char *client_id) {
    return (gs_oauth_client){ .device_url = "https://id.twitch.tv/oauth2/device", .token_url = "https://id.twitch.tv/oauth2/token",
                              .client_id = client_id, .scope = TWITCH_SCOPES, .scope_field = "scopes" };
}

twitch_error twitch_validate(const char *client_id, twitch_session *s, char *message, size_t size) {
    char auth[600];
    SDL_snprintf(auth, sizeof auth, "Authorization: OAuth %s", s->access);
    const char *headers[] = { auth, NULL };
    char *body = NULL;
    size_t len = 0;
    int status = gs_http_fetch(&(gs_http_request){ .url = "https://id.twitch.tv/oauth2/validate", .headers = headers, .timeout_ms = 15000 }, &body, &len);
    gs_json *doc = body ? gs_json_parse(body, len) : NULL;
    SDL_free(body);
    gs_jv root = gs_json_root(doc);
    twitch_error e = TW_OK;
    if (!status || status >= 500) e = TW_NETWORK, say(message, size, "no answer from Twitch");
    else if (status == 401) e = TW_LOGIN_REQUIRED, say(message, size, "Twitch no longer accepts this session");
    else if (status != 200) e = TW_UNKNOWN, say(message, size, gs_json_str(gs_json_get(root, "message"), "Twitch could not check the session"));
    else if (strcmp(gs_json_str(gs_json_get(root, "client_id"), ""), client_id)) e = TW_LOGIN_REQUIRED, say(message, size, "the session belongs to another application");
    else {
        SDL_strlcpy(s->login, gs_json_str(gs_json_get(root, "login"), ""), sizeof s->login);
        SDL_strlcpy(s->user_id, gs_json_str(gs_json_get(root, "user_id"), ""), sizeof s->user_id);
        if (!s->user_id[0]) e = TW_UNKNOWN, say(message, size, "Twitch did not say whose session this is");
    }
    gs_json_free(doc);
    return e;
}

static twitch_error oauth_error(gs_oauth_result r) {
    return r == GS_OAUTH_OK ? TW_OK : r == GS_OAUTH_NETWORK ? TW_NETWORK : r == GS_OAUTH_ERROR ? TW_UNKNOWN : TW_LOGIN_REQUIRED;
}

twitch_error twitch_refresh(const char *client_id, twitch_session *s, char *message, size_t size) {
    if (!s->refresh[0]) return say(message, size, "the session cannot be renewed"), TW_LOGIN_REQUIRED;
    gs_oauth_client c = twitch_oauth_client(client_id);
    gs_oauth_token t = { 0 };
    twitch_error e = oauth_error(gs_oauth_refresh(&c, s->refresh, &t, message, size));
    if (e == TW_OK) SDL_strlcpy(s->access, t.access_token, sizeof s->access), SDL_strlcpy(s->refresh, t.refresh_token, sizeof s->refresh);
    SDL_memset(&t, 0, sizeof t);
    return e;
}

twitch_error twitch_session_from(const char *client_id, const gs_oauth_token *t, twitch_session *s, char *message, size_t size) {
    memset(s, 0, sizeof *s);
    SDL_strlcpy(s->access, t->access_token, sizeof s->access);
    SDL_strlcpy(s->refresh, t->refresh_token, sizeof s->refresh);
    return twitch_validate(client_id, s, message, size);
}

// ---- Followed channels that are live ----

int twitch_parse_streams(const char *json, size_t len, twitch_stream *out, int max, char *cursor, size_t cursor_size) {
    gs_json *doc = gs_json_parse(json, len);
    gs_jv root = gs_json_root(doc), data = gs_json_get(root, "data");
    if (gs_json_kind(data) != GS_JSON_ARRAY) return gs_json_free(doc), -1;
    int n = 0;
    for (gs_jv it = gs_json_first(data); it; it = gs_json_next(it), n++) {
        if (n >= max) continue;
        twitch_stream *s = &out[n];
        memset(s, 0, sizeof *s);
        SDL_strlcpy(s->login, gs_json_str(gs_json_get(it, "user_login"), ""), sizeof s->login);
        SDL_strlcpy(s->name, gs_json_str(gs_json_get(it, "user_name"), ""), sizeof s->name);
        SDL_strlcpy(s->user_id, gs_json_str(gs_json_get(it, "user_id"), ""), sizeof s->user_id);
        SDL_strlcpy(s->title, gs_json_str(gs_json_get(it, "title"), ""), sizeof s->title);
        SDL_strlcpy(s->category, gs_json_str(gs_json_get(it, "game_name"), ""), sizeof s->category);
        SDL_strlcpy(s->language, gs_json_str(gs_json_get(it, "language"), ""), sizeof s->language);
        SDL_strlcpy(s->started_at, gs_json_str(gs_json_get(it, "started_at"), ""), sizeof s->started_at);
        SDL_strlcpy(s->thumbnail, gs_json_str(gs_json_get(it, "thumbnail_url"), ""), sizeof s->thumbnail);
        s->viewers = (int)gs_json_num(gs_json_get(it, "viewer_count"), 0);
        s->mature = gs_json_bool(gs_json_get(it, "is_mature"), false);
    }
    if (cursor_size) SDL_strlcpy(cursor, gs_json_str(gs_json_path(root, "pagination.cursor"), ""), cursor_size);
    gs_json_free(doc);
    return n;
}

// One Helix GET with the session; *status gets the HTTP status.
static char *helix_get(const char *client_id, const twitch_session *s, const char *url, int *status, size_t *len) {
    char id[128], auth[600];
    SDL_snprintf(id, sizeof id, "Client-Id: %s", client_id);
    SDL_snprintf(auth, sizeof auth, "Authorization: Bearer %s", s->access);
    const char *headers[] = { id, auth, NULL };
    char *body = NULL;
    *status = gs_http_fetch(&(gs_http_request){ .url = url, .headers = headers, .timeout_ms = 15000 }, &body, len);
    return body;
}

twitch_error twitch_followed_live(const char *client_id, twitch_session *s, twitch_stream *out, int max, int *count, char *message, size_t size) {
    *count = 0;
    char cursor[256] = "";
    bool renewed = false;
    for (int page = 0; page < 50; page++) {  // (50 pages of 100: far beyond anyone's live follows)
        char url[600];
        SDL_snprintf(url, sizeof url, "https://api.twitch.tv/helix/streams/followed?user_id=%s&first=100%s%s", s->user_id, cursor[0] ? "&after=" : "", cursor);
        int status;
        size_t len = 0;
        char *body = helix_get(client_id, s, url, &status, &len);
        if (status == 401 && !renewed) {  // the access token expired: renew once and ask again
            SDL_free(body);
            renewed = true;
            twitch_error e = twitch_refresh(client_id, s, message, size);
            if (e != TW_OK) return e;
            page--;
            continue;
        }
        twitch_error e = !status || status >= 500 ? TW_NETWORK : status == 401 ? TW_LOGIN_REQUIRED : status == 429 ? TW_RATE_LIMITED : status != 200 ? TW_UNKNOWN : TW_OK;
        int n = e == TW_OK ? twitch_parse_streams(body, len, out + *count, max - *count, cursor, sizeof cursor) : -1;
        SDL_free(body);
        if (e == TW_OK && n < 0) e = TW_UNKNOWN;
        if (e != TW_OK) return say(message, size, twitch_error_text(e)), e;
        *count += n < max - *count ? n : max - *count;
        if (!cursor[0] || *count >= max) break;
    }
    return TW_OK;
}

void twitch_thumbnail_url(const char *template_url, int width, int height, char *out, size_t size) {
    size_t n = 0;
    for (const char *p = template_url; *p && n + 1 < size;) {
        if (!strncmp(p, "{width}", 7)) n += SDL_snprintf(out + n, size - n, "%d", width), p += 7;
        else if (!strncmp(p, "{height}", 8)) n += SDL_snprintf(out + n, size - n, "%d", height), p += 8;
        else out[n++] = *p++;
    }
    if (n >= size) n = size - 1;
    out[n] = 0;
}

// ---- Playback ----

static twitch_error classify(const char *text) {
    if (SDL_strcasestr(text, "integrity") || SDL_strcasestr(text, "client-id") || SDL_strcasestr(text, "client id")) return TW_UNSUPPORTED;
    if (SDL_strcasestr(text, "geo")) return TW_GEO_BLOCKED;
    if (SDL_strcasestr(text, "mature") || SDL_strcasestr(text, "age")) return TW_AGE_RESTRICTED;
    if (SDL_strcasestr(text, "sub") || SDL_strcasestr(text, "entitlement")) return TW_SUBSCRIBER_ONLY;
    if (SDL_strcasestr(text, "login") || SDL_strcasestr(text, "unauthor")) return TW_LOGIN_REQUIRED;
    return TW_UNKNOWN;
}

twitch_error twitch_parse_token(int status, const char *json, size_t len, char *signature, size_t sig_size,
                                char *value, size_t value_size, long long *expires_at, char *message, size_t size) {
    if (!status) return say(message, size, "no answer from Twitch"), TW_NETWORK;
    if (status == 429) return say(message, size, twitch_error_text(TW_RATE_LIMITED)), TW_RATE_LIMITED;
    if (status >= 500) return say(message, size, "Twitch's playback service failed"), TW_NETWORK;
    gs_json *doc = json ? gs_json_parse(json, len) : NULL;
    gs_jv root = gs_json_root(doc);
    twitch_error e = TW_OK;
    const char *said = gs_json_str(gs_json_path(root, "errors.0.message"), gs_json_str(gs_json_get(root, "message"), ""));
    const char *error = gs_json_str(gs_json_get(root, "error"), "");
    gs_jv token = gs_json_path(root, "data.streamPlaybackAccessToken");
    if (!root) {
        e = status == 401 || status == 403 ? TW_LOGIN_REQUIRED : TW_NETWORK;
        say(message, size, "Twitch's playback answer could not be read");
    } else if (said[0] || error[0]) {
        char both[512];
        SDL_snprintf(both, sizeof both, "%s %s", error, said);
        e = classify(both);
        if (e == TW_UNKNOWN && (status == 401 || status == 403)) e = TW_LOGIN_REQUIRED;
        say(message, size, said[0] ? said : error);
    } else if (gs_json_kind(token) != GS_JSON_OBJECT) {
        e = TW_OFFLINE, say(message, size, twitch_error_text(TW_OFFLINE));
    } else {
        SDL_strlcpy(signature, gs_json_str(gs_json_get(token, "signature"), ""), sig_size);
        SDL_strlcpy(value, gs_json_str(gs_json_get(token, "value"), ""), value_size);
        if (!signature[0] || !value[0]) e = TW_UNKNOWN, say(message, size, "Twitch's playback token was incomplete");
        // The token's value is itself JSON, with the time it runs out.
        gs_json *inner = e == TW_OK ? gs_json_parse(value, strlen(value)) : NULL;
        if (expires_at) *expires_at = (long long)gs_json_num(gs_json_get(gs_json_root(inner), "expires"), 0);
        gs_json_free(inner);
    }
    gs_json_free(doc);
    return e;
}

void twitch_variant_name(const gs_hls_variant *v, char *out, size_t size) {
    if (gs_hls_attr(v->attrs, "STABLE-VARIANT-ID", out, size) || gs_hls_attr(v->attrs, "IVS-NAME", out, size)) return;
    if (v->name[0]) SDL_strlcpy(out, v->name, size);
    else if (v->audio_only) SDL_strlcpy(out, "audio_only", size);
    else SDL_snprintf(out, size, "%dp%d", v->height, (int)SDL_lround(v->fps));
}

static long long score(const gs_hls_variant *v) { return (long long)v->height * 1000000000LL + SDL_lround(v->fps) * 1000000LL + v->bandwidth; }

static bool is_source(const gs_hls_variant *v) {
    char value[64];
    return (gs_hls_attr(v->attrs, "IVS-VARIANT-SOURCE", value, sizeof value) && !SDL_strcasecmp(value, "source")) ||
           SDL_strcasestr(v->name, "source") || SDL_strcasestr(v->name, "chunked");
}

// One quality from the list: the index of the best (or worst) matching rendition, or -1.
static int pick_one(const gs_hls_variant *v, int count, const char *q) {
    enum { BEST, WORST, SOURCE, AUDIO, HEIGHT } kind;
    int height = 0, fps = -1;
    if (!SDL_strcasecmp(q, "best")) kind = BEST;
    else if (!SDL_strcasecmp(q, "worst")) kind = WORST;
    else if (!SDL_strcasecmp(q, "source") || !SDL_strcasecmp(q, "chunked")) kind = SOURCE;
    else if (!SDL_strcasecmp(q, "audio") || !SDL_strcasecmp(q, "audio_only") || !SDL_strcasecmp(q, "audio-only")) kind = AUDIO;
    else {
        char *p;
        height = (int)SDL_strtol(q, &p, 10);
        if (height <= 0 || (*p != 'p' && *p != 'P')) return -1;
        if (p[1]) {
            char *e;
            fps = (int)SDL_strtol(p + 1, &e, 10);
            if (*e) return -1;
        }
        kind = HEIGHT;
    }
    int chosen = -1;
    for (int i = 0; i < count; i++) {
        bool ok = kind == AUDIO ? v[i].audio_only
                : kind == HEIGHT ? !v[i].audio_only && v[i].height == height && (fps < 0 || SDL_lround(v[i].fps) == fps)
                : kind == SOURCE ? !v[i].audio_only && is_source(&v[i])
                : !v[i].audio_only;
        if (ok && (chosen < 0 || (kind == WORST ? score(&v[i]) < score(&v[chosen]) : score(&v[i]) > score(&v[chosen])))) chosen = i;
    }
    if (chosen < 0 && kind == SOURCE) return pick_one(v, count, "best");
    return chosen;
}

int twitch_pick_variant(const gs_hls_variant *v, int count, const char *quality) {
    char list[256], *save;
    SDL_strlcpy(list, quality && quality[0] ? quality : "best", sizeof list);
    for (char *q = SDL_strtok_r(list, ",", &save); q; q = SDL_strtok_r(NULL, ",", &save)) {
        while (*q == ' ') q++;
        int i = pick_one(v, count, q);
        if (i >= 0) return i;
    }
    return -1;
}

twitch_error twitch_resolve(const char *channel, const char *quality, bool low_latency, twitch_playback *out, char *message, size_t size) {
    char login[64];
    twitch_normalize_login(channel, login, sizeof login);
    if (!twitch_valid_login(login)) return say(message, size, "not a Twitch channel name"), TW_UNKNOWN;
    char body[640];
    SDL_snprintf(body, sizeof body,
                 "{\"operationName\":\"PlaybackAccessToken\",\"extensions\":{\"persistedQuery\":{\"version\":1,\"sha256Hash\":\"" TOKEN_QUERY_HASH "\"}},"
                 "\"variables\":{\"isLive\":true,\"login\":\"%s\",\"isVod\":false,\"vodID\":\"\",\"playerType\":\"embed\",\"platform\":\"site\"}}", login);
    const char *gql_headers[] = { "Client-ID: " WEB_CLIENT_ID, "Content-Type: application/json", NULL };
    char *answer = NULL;
    size_t len = 0;
    int status = gs_http_fetch(&(gs_http_request){ .url = "https://gql.twitch.tv/gql", .agent = AGENT, .headers = gql_headers, .body = body, .timeout_ms = 15000 }, &answer, &len);
    char sig[256], *token = SDL_malloc(4096);  // (the token, about 1 KB, is too big for a worker's stack frame twice over)
    if (!token) return SDL_free(answer), TW_UNKNOWN;
    twitch_error e = twitch_parse_token(status, answer, len, sig, sizeof sig, token, 4096, &out->expires_at, message, size);
    SDL_free(answer);
    if (e != TW_OK) return SDL_free(token), e;

    char sig_q[600], token_q[8192], url[10000];
    gs_oauth_encode(sig, sig_q, sizeof sig_q);
    gs_oauth_encode(token, token_q, sizeof token_q);
    SDL_free(token);
    SDL_snprintf(url, sizeof url,
                 "https://usher.ttvnw.net/api/v2/channel/hls/%s.m3u8?platform=web&p=%d&allow_source=true&allow_audio_only=true"
                 "&playlist_include_framerate=true&supported_codecs=h264&sig=%s&token=%s%s",
                 login, (int)SDL_rand(1000000), sig_q, token_q, low_latency ? "&fast_bread=true" : "");
    const char *usher_headers[] = { "Referer: https://player.twitch.tv", "Origin: https://player.twitch.tv", NULL };
    status = gs_http_fetch(&(gs_http_request){ .url = url, .agent = AGENT, .headers = usher_headers, .timeout_ms = 15000 }, &answer, &len);
    if (status == 404) e = TW_OFFLINE, say(message, size, twitch_error_text(TW_OFFLINE));
    else if (!status || status >= 500) e = TW_NETWORK, say(message, size, "no answer from Twitch's playlist service");
    else if (status == 429) e = TW_RATE_LIMITED, say(message, size, twitch_error_text(e));
    else if (status != 200) {
        e = classify(answer ? answer : "");
        if (e == TW_UNKNOWN && status == 403) e = TW_LOGIN_REQUIRED;
        say(message, size, twitch_error_text(e));
    } else {
        if (SDL_strlcpy(out->master, url, sizeof out->master) >= sizeof out->master) out->master[0] = 0;
        out->count = gs_hls_master(answer, len, url, out->variants, (int)SDL_arraysize(out->variants));
        if (out->count > (int)SDL_arraysize(out->variants)) out->count = (int)SDL_arraysize(out->variants);
        int i = out->count > 0 ? twitch_pick_variant(out->variants, out->count, quality) : -1;
        if (i < 0) e = TW_UNSUPPORTED, say(message, size, "Twitch sent no playable rendition");
        else SDL_strlcpy(out->url, out->variants[i].url, sizeof out->url), twitch_variant_name(&out->variants[i], out->quality, sizeof out->quality);
    }
    SDL_free(answer);
    return e;
}
