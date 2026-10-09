// Twitch: signing in with the device code flow, the followed channels that are live (Helix), and a
// channel's playable stream (the web player's playback token and Usher's HLS playlists). Requests
// block, so call them from worker threads. The parsing functions take no network and are tested.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "gs_hls.h"
#include "gs_oauth.h"

#define TWITCH_SCOPES "user:read:follows"

typedef enum {
    TW_OK,
    TW_OFFLINE,          // the channel is not live
    TW_LOGIN_REQUIRED,   // the session is gone or refused: sign in again
    TW_SUBSCRIBER_ONLY,
    TW_GEO_BLOCKED,
    TW_AGE_RESTRICTED,
    TW_RATE_LIMITED,
    TW_UNSUPPORTED,      // Twitch refused this client (a failed integrity check, say) or sent nothing playable
    TW_NETWORK,          // no answer, or a server error: try again later
    TW_UNKNOWN,
} twitch_error;

const char *twitch_error_text(twitch_error e);

// ---- Logins ----

bool twitch_valid_login(const char *login);  // 1 to 25 letters, digits and underscores
void twitch_normalize_login(const char *login, char *out, size_t size);  // lowercased

// ---- The session ----

typedef struct {
    char access[512], refresh[512];
    char user_id[32], login[64];
} twitch_session;

// Kept with gs_secret in the data folder (dir ends with a separator).
bool twitch_session_load(const char *dir, twitch_session *s);
bool twitch_session_save(const char *dir, const twitch_session *s);
void twitch_session_erase(const char *dir);

gs_oauth_client twitch_oauth_client(const char *client_id);

// Checks the access token with Twitch and fills in the user it belongs to. TW_LOGIN_REQUIRED when
// the token is invalid or belongs to another application.
twitch_error twitch_validate(const char *client_id, twitch_session *s, char *message, size_t size);
// Renews the access token with the refresh token; TW_LOGIN_REQUIRED when Twitch refuses it.
twitch_error twitch_refresh(const char *client_id, twitch_session *s, char *message, size_t size);
// A session from the tokens the device flow returned: validated, then saved by the caller.
twitch_error twitch_session_from(const char *client_id, const gs_oauth_token *t, twitch_session *s, char *message, size_t size);

// ---- Followed channels that are live ----

typedef struct {
    char login[64], name[128], user_id[32];
    char title[600], category[256], language[16];
    char started_at[32];   // RFC 3339, UTC
    char thumbnail[512];   // a URL template with {width} and {height}
    int viewers;
    bool mature;
} twitch_stream;

// Parses one page of Helix's /streams/followed: returns how many streams it holds (up to max are
// filled), or -1 when the text is not such a page. *cursor gets the next page's cursor, or "".
int twitch_parse_streams(const char *json, size_t len, twitch_stream *out, int max, char *cursor, size_t cursor_size);

// All followed channels that are live, through every page; renews the session once when Twitch
// says its token expired (the caller saves the changed session).
twitch_error twitch_followed_live(const char *client_id, twitch_session *s, twitch_stream *out, int max, int *count, char *message, size_t size);

// Fills a thumbnail URL template in for a width and height.
void twitch_thumbnail_url(const char *template_url, int width, int height, char *out, size_t size);

// ---- Playback ----

typedef struct {
    char url[2048];        // the chosen rendition's media playlist
    char master[4096];     // the master playlist with every rendition, for adaptive play; "" when too long to keep
    char quality[64];      // its name, such as "1080p60" or "audio_only"
    long long expires_at;  // Unix time the playback token runs out; 0 when unknown
    gs_hls_variant variants[16];
    int count;
} twitch_playback;

// Classifies the GraphQL playback token answer. On TW_OK, signature and value hold the token.
twitch_error twitch_parse_token(int status, const char *json, size_t len, char *signature, size_t sig_size,
                                char *value, size_t value_size, long long *expires_at, char *message, size_t size);

// A rendition's name: Twitch's STABLE-VARIANT-ID or IVS-NAME attribute, the video group's name,
// or one made from its height and frame rate.
void twitch_variant_name(const gs_hls_variant *v, char *out, size_t size);

// Picks from a comma-separated list of qualities tried in turn: "best", "worst", "source",
// "audio_only", or a height with an optional frame rate such as "720p60" or "480p". -1 when none fits.
int twitch_pick_variant(const gs_hls_variant *v, int count, const char *quality);

// Resolves a channel's stream: the playback token, the master playlist and the rendition.
twitch_error twitch_resolve(const char *login, const char *quality, bool low_latency, twitch_playback *out, char *message, size_t size);
