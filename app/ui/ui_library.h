// SPDX-License-Identifier: MPL-2.0
// Private to app/ui: the Music browse screens (ui_library.c), over the library service (library_service.h).
#ifndef NEUPOD_UI_LIBRARY_H
#define NEUPOD_UI_LIBRARY_H

#include "library_view.h"
#include "ui_internal.h"

/** One list of the library (Artists, an artist's albums, an album's songs, ...). @p kind is an lvw_kind_t, @p title what the status bar shows until the
 *  list knows its own. Opens the index; with no index yet, a top-level list builds it (with a progress screen and Cancel) once nothing plays. */
ui_screen_t *ui_screen_library_view_create(uint8_t kind, uint32_t id, const char *title);
/** Music > Update Library: rebuild the index now, with progress and Cancel; says what it found, or why it could not. */
ui_screen_t *ui_screen_library_update_create(void);
/** One Playlists list (ui_playlists.c): @p kind a pls_kind_t, @p file the playlist file's number when it is PLS_FILE, @p name what the status bar shows. */
ui_screen_t *ui_screen_playlist_view_create(uint8_t kind, uint8_t file, const char *name);
/** Home > Shuffle Songs: play the whole library in random order (building the index first if there is none). */
ui_screen_t *ui_screen_shuffle_create(void);

#endif
