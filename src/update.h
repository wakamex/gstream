// Keeps a published copy of gstream up to date by itself, as gear and gsignal do. Every minute it
// reads a manifest from the update server: the build it describes and the name, SHA-256 and size of
// each file of that build, signed with the publisher's Ed25519 key. When the build is another and the
// signature holds, each file that differs is downloaded and checked against its hash, and once all
// have arrived each is put in place of the old one, which is renamed aside (a running program on
// Windows can be renamed but not overwritten). The app then quits and starts the new build.
//
// The server is reached over plain HTTP: the signature, made with a key that never leaves the
// publisher's machine, is what keeps a changed file out, whoever answers.
#pragma once
#include <stdbool.h>

typedef struct gstream_update gstream_update;

// The build this program is, as tools/publish-update named it, or NULL for any other build (a
// release, or one's own), which never updates itself.
const char *gstream_update_build(void);
// Starts looking for updates for the program in `folder` (ending in a separator), when this is a
// published build; NULL otherwise. `ready` is called, on another thread, once a newer build is in
// place. GSTREAM_UPDATE_FROM names another server, for trying it out.
gstream_update *gstream_update_start(const char *folder, void (*ready)(void));
bool gstream_update_ready(gstream_update *u);  // a newer build is in place, to be started
void gstream_update_free(gstream_update *u);   // stops looking; waits for a download under way to end

// Starts the program in `folder` again, going on with the channel that was playing, for the new
// build to take over.
bool gstream_update_restart(const char *folder);
