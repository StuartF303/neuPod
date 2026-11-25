// SPDX-License-Identifier: MPL-2.0
/**
 * @file pl_m3u.h
 * @brief The .m3u / .m3u8 reader behind Music > Playlists (design review LB-7), as a pure byte-fed parser: no file access, no allocation, host-tested.
 *
 * Accepts what players write in practice: an optional UTF-8 byte-order mark and "#EXTM3U" header, "#EXTINF:seconds,Artist - Title" lines (taken as the
 * entry's title and artist when present; every other '#' line is a comment), blank lines, LF / CRLF / CR line ends, absolute paths ("/Music/a.flac"), paths
 * relative to the playlist's own folder ("../Other/b.mp3", "c.wav"), Windows separators, a drive letter ("C:\Music\a.flac": the drive is dropped and the rest
 * is read from the card's root) and "file://" URLs (percent escapes decoded). A line that is valid UTF-8 is kept as it is; any other line is read as Latin-1
 * (old .m3u files), so accents survive either way. A path that cannot name a file on the card (another URL scheme, one that climbs above the root, one that is
 * too long) is still an entry, marked unresolvable: the list greys it instead of hiding it, so the count stays honest.
 */
#ifndef NEUPOD_PL_M3U_H
#define NEUPOD_PL_M3U_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PL_PATH_MAX 256u /**< a resolved path, NUL included (the file system's own limit) */
#define PL_LINE_MAX 384u /**< longest line kept; a longer one is an unresolvable entry (or a cut comment) */
#define PL_TITLE_MAX 48u
#define PL_ARTIST_MAX 32u

typedef struct {
    char path[PL_PATH_MAX];     /**< absolute, normalised ("" when unresolvable) */
    char title[PL_TITLE_MAX];   /**< from #EXTINF, "" = derive from the file name (an unresolvable line: what the line said) */
    char artist[PL_ARTIST_MAX]; /**< from #EXTINF ("Artist - Title"), "" = none */
    uint32_t secs;              /**< from #EXTINF, 0 = unknown */
    bool resolvable;            /**< path names a place on the card (not that a file exists there: the caller checks) */
} pl_entry_t;

typedef void (*pl_entry_fn)(void *user, const pl_entry_t *e);

typedef struct {
    char dir[PL_PATH_MAX]; /**< the playlist's folder: "/" or "/a/b" */
    char line[PL_LINE_MAX];
    size_t n;
    bool over;      /**< the current line is longer than PL_LINE_MAX */
    bool started;   /**< the first byte was seen (a byte-order mark is only skipped there) */
    pl_entry_t inf; /**< the pending #EXTINF, applied to the next path line */
    bool have_inf;
    uint32_t entries; /**< entries found so far */
} pl_m3u_t;

/** Start a parse of a playlist that lives in folder @p dir ("/" or "/a/b"). */
void pl_m3u_init(pl_m3u_t *p, const char *dir);
/** Feed @p n bytes; @p fn is called for every entry completed by them. */
void pl_m3u_feed(pl_m3u_t *p, const uint8_t *data, size_t n, pl_entry_fn fn, void *user);
/** The file ended: a last line without a line end counts. */
void pl_m3u_end(pl_m3u_t *p, pl_entry_fn fn, void *user);

/** Resolve @p raw (one playlist line, already UTF-8) against @p dir into @p out (absolute, normalised, no trailing '/'). False when it cannot name a place on the card. */
bool pl_resolve(const char *dir, const char *raw, char *out, size_t n);
/** True for a name ending in .m3u or .m3u8 (any case). */
bool pl_is_playlist_name(const char *name);
/** One entry as playlist text in @p out (at most @p n bytes incl. NUL): "#EXTINF:secs,Artist - Title\n<path>\n". Returns the length, 0 if it does not fit. */
size_t pl_format_entry(char *out, size_t n, const char *path, const char *title, const char *artist, uint32_t secs);
/** "#EXTM3U\n" header text. */
extern const char pl_m3u_header[];

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PL_M3U_H
