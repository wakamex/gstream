#include <string.h>

#include "test.h"
#include "twitch.h"

// The older master playlist form, with EXT-X-MEDIA video groups.
static const char groups[] =
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"chunked\",NAME=\"1080p60 (source)\",AUTOSELECT=YES,DEFAULT=YES\n"
    "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"720p60\",NAME=\"720p60\"\n"
    "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"audio_only\",NAME=\"Audio Only\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=6500000,AVERAGE-BANDWIDTH=6100000,RESOLUTION=1920x1080,FRAME-RATE=60.000,VIDEO=\"chunked\",CODECS=\"avc1.64002A,mp4a.40.2\"\n"
    "source/index-live.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3500000,RESOLUTION=1280x720,FRAME-RATE=60.000,VIDEO=\"720p60\",CODECS=\"avc1.4D4020,mp4a.40.2\"\n"
    "https://video.example/720p60/index-live.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=160000,VIDEO=\"audio_only\",CODECS=\"mp4a.40.2\"\n"
    "/audio/index-live.m3u8\n";

// The current form, naming renditions with STABLE-VARIANT-ID.
static const char stable[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=7982196,RESOLUTION=1920x1080,CODECS=\"avc1.64002A,mp4a.40.2\",FRAME-RATE=60.000,STABLE-VARIANT-ID=\"1080p60\",IVS-VARIANT-SOURCE=\"source\"\n"
    "https://cdn.example/1080.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3422999,RESOLUTION=1280x720,CODECS=\"avc1.4D401F,mp4a.40.2\",FRAME-RATE=60.000,STABLE-VARIANT-ID=\"720p60\",IVS-VARIANT-SOURCE=\"transcode\"\n"
    "https://cdn.example/720.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1427999,RESOLUTION=852x480,CODECS=\"avc1.4D401F,mp4a.40.2\",FRAME-RATE=30.000,STABLE-VARIANT-ID=\"480p30\"\n"
    "https://cdn.example/480.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=160000,CODECS=\"mp4a.40.2\",STABLE-VARIANT-ID=\"audio_only\"\n"
    "https://cdn.example/audio.m3u8\n";

static const char *pick(const gs_hls_variant *v, int n, const char *quality, char *name, size_t size) {
    int i = twitch_pick_variant(v, n, quality);
    if (i < 0) return "";
    twitch_variant_name(&v[i], name, size);
    return name;
}

static twitch_error token(int status, const char *json) {
    char sig[64], value[512], msg[128];
    long long expires = 0;
    return twitch_parse_token(status, json, json ? strlen(json) : 0, sig, sizeof sig, value, sizeof value, &expires, msg, sizeof msg);
}

void test_twitch(void) {
    CHECK(twitch_valid_login("cohhcarnage") && twitch_valid_login("Some_User_42"));
    CHECK(!twitch_valid_login("") && !twitch_valid_login("bad/user") && !twitch_valid_login("abcdefghijklmnopqrstuvwxyz"));
    char login[32];
    twitch_normalize_login("Some_User_42", login, sizeof login);
    CHECK(!strcmp(login, "some_user_42"));

    gs_hls_variant v[8];
    char name[64];
    int n = gs_hls_master(groups, sizeof groups - 1, "https://usher.example/api/channel/hls/cohh.m3u8?token=abc", v, 8);
    CHECK(n == 3);
    CHECK(!strcmp(v[0].url, "https://usher.example/api/channel/hls/source/index-live.m3u8"));
    CHECK(!strcmp(v[2].url, "https://usher.example/audio/index-live.m3u8") && v[2].audio_only);
    CHECK(!strcmp(pick(v, n, "best", name, sizeof name), "1080p60 (source)"));
    CHECK(!strcmp(pick(v, n, "source", name, sizeof name), "1080p60 (source)"));
    CHECK(!strcmp(pick(v, n, "720p60,best", name, sizeof name), "720p60"));
    CHECK(!strcmp(pick(v, n, "480p,best", name, sizeof name), "1080p60 (source)"));  // falls back
    CHECK(!strcmp(pick(v, n, "audio_only", name, sizeof name), "Audio Only"));
    CHECK(!strcmp(pick(v, n, "worst", name, sizeof name), "720p60"));  // ignores audio

    n = gs_hls_master(stable, sizeof stable - 1, "https://usher.example/x.m3u8", v, 8);
    CHECK(n == 4);
    CHECK(!strcmp(pick(v, n, "best", name, sizeof name), "1080p60"));
    CHECK(!strcmp(pick(v, n, "source", name, sizeof name), "1080p60"));
    CHECK(!strcmp(pick(v, n, "480p30", name, sizeof name), "480p30"));
    CHECK(!strcmp(pick(v, n, "720p", name, sizeof name), "720p60"));
    CHECK(!strcmp(pick(v, n, "audio_only", name, sizeof name), "audio_only"));
    CHECK(twitch_pick_variant(v, n, "1440p60") < 0);

    CHECK(token(200, "{\"data\":{\"streamPlaybackAccessToken\":{\"value\":\"{\\\"expires\\\":1791480000}\",\"signature\":\"abc\"}}}") == TW_OK);
    CHECK(token(200, "{\"data\":{\"streamPlaybackAccessToken\":null}}") == TW_OFFLINE);
    CHECK(token(403, "{\"errors\":[{\"message\":\"failed integrity check\"}]}") == TW_UNSUPPORTED);
    CHECK(token(403, "{\"error\":\"Forbidden\",\"message\":\"login required\"}") == TW_LOGIN_REQUIRED);
    CHECK(token(0, NULL) == TW_NETWORK);
    CHECK(token(429, "") == TW_RATE_LIMITED);
    char sig[64], value[512], msg[128];
    long long expires = 0;
    const char *ok = "{\"data\":{\"streamPlaybackAccessToken\":{\"value\":\"{\\\"expires\\\":1791480000}\",\"signature\":\"abc\"}}}";
    twitch_parse_token(200, ok, strlen(ok), sig, sizeof sig, value, sizeof value, &expires, msg, sizeof msg);
    CHECK(expires == 1791480000 && !strcmp(sig, "abc"));

    const char page[] =
        "{\"data\":[{\"id\":\"1\",\"user_id\":\"7\",\"user_login\":\"moonmoon\",\"user_name\":\"MOONMOON\",\"game_name\":\"Just Chatting\","
        "\"type\":\"live\",\"title\":\"caf\\u00e9\",\"viewer_count\":12345,\"started_at\":\"2026-10-08T12:00:00Z\",\"language\":\"en\","
        "\"thumbnail_url\":\"https://static.example/live_user_moonmoon-{width}x{height}.jpg\",\"tags\":[],\"is_mature\":true}],"
        "\"pagination\":{\"cursor\":\"next-page\"}}";
    twitch_stream s[2];
    char cursor[64];
    CHECK(twitch_parse_streams(page, sizeof page - 1, s, 2, cursor, sizeof cursor) == 1);
    CHECK(!strcmp(s[0].login, "moonmoon") && !strcmp(s[0].name, "MOONMOON") && s[0].viewers == 12345 && s[0].mature);
    CHECK(!strcmp(s[0].title, "caf\xc3\xa9") && !strcmp(cursor, "next-page"));
    char url[256];
    twitch_thumbnail_url(s[0].thumbnail, 320, 180, url, sizeof url);
    CHECK(!strcmp(url, "https://static.example/live_user_moonmoon-320x180.jpg"));
    CHECK(twitch_parse_streams("{\"error\":\"x\"}", 13, s, 2, cursor, sizeof cursor) == -1);
}
