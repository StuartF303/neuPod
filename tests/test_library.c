// SPDX-License-Identifier: MPL-2.0
// Music library index (docs/adr/0017-music-library-index.md, L1): collation, the new tag fields of every format, grouping rules, a build
// and read round trip on a FatFs disk image, and the swap surviving a power cut at every step. The 100k-entry build is test_library_big.c.
#include "library_fixture.h"

#include "library.h"
#include "meta.h"
#include "unit.h"

/* ---- collation ----------------------------------------------------------------------------------------------------- */

static int cmp(const char *a, const char *b) { return lib_collate_cmp(a, NULL, b, NULL); }

TEST(collate_case_and_the) {
    ASSERT_EQ(0, cmp("abba", "ABBA"));
    ASSERT_EQ(0, cmp("The Beatles", "Beatles"));
    ASSERT_EQ(0, cmp("THE BEATLES", "beatles"));
    ASSERT_TRUE(cmp("The Band", "Beatles") < 0);   /* B-a-n < B-e-a */
    ASSERT_TRUE(cmp("Theatre of Tragedy", "Them") < 0); /* only "The " with the space is dropped: "theatre" < "them" */
    ASSERT_TRUE(cmp("Theory", "Therapy") < 0);
    ASSERT_TRUE(cmp("The ", "A") > 0); /* "The " alone is a name, not an article */
}

TEST(collate_diacritics) {
    ASSERT_EQ(0, cmp("\xC3\x89mile", "Emile"));            /* É */
    ASSERT_EQ(0, cmp("Beyonc\xC3\xA9", "Beyonce"));
    ASSERT_EQ(0, cmp("\xC3\x9F", "ss"));                   /* ß */
    ASSERT_EQ(0, cmp("\xC3\x86on", "Aeon"));               /* Æ */
    ASSERT_EQ(0, cmp("Sigur R\xC3\xB3s", "Sigur Ros"));
    ASSERT_EQ(0, cmp("\xC3\x98rsted", "Orsted"));          /* Ø */
    ASSERT_EQ(0, cmp("Dvo\xC5\x99\xC3\xA1k", "Dvorak"));   /* Latin Extended-A: ř */
    ASSERT_EQ(0, cmp("\xC5\x81\xC3\xB3" "d\xC5\xBA", "Lodz")); /* Ł ó ź */
    ASSERT_EQ(0, cmp("\xC5\x92uvre", "Oeuvre"));           /* Œ */
    ASSERT_EQ(0, cmp("\xC4\xB2", "ij"));                   /* Ĳ */
    ASSERT_TRUE(cmp("\xC3\x89", "F") < 0);
    ASSERT_TRUE(cmp("Zo\xC3\xA9", "Zoe") == 0);
}

TEST(collate_classes) {
    ASSERT_TRUE(cmp("10cc", "Abba") < 0); /* digits before letters */
    ASSERT_TRUE(cmp("99 Luftballons", "Abba") < 0);
    ASSERT_TRUE(cmp("!!!", "10cc") < 0);  /* symbols before digits */
    ASSERT_TRUE(cmp(" leading space", "a") < 0);
    ASSERT_TRUE(cmp("a b", "ab") < 0);    /* a space sorts first */
    ASSERT_TRUE(cmp("&", "~") < 0);       /* ASCII symbols by code point */
    ASSERT_TRUE(cmp("~", "\xCE\xB1") < 0); /* Greek alpha: other, after the ASCII symbols, before digits */
    ASSERT_TRUE(cmp("\xCE\xB1", "0") < 0);
    ASSERT_TRUE(cmp("\xCE\xB1", "\xCE\xB2") < 0); /* alpha < beta by code point */
    ASSERT_TRUE(cmp("\xE3\x81\x82", "\xF0\x9F\x8E\xB5") < 0); /* hiragana a < musical-note emoji */
}

TEST(collate_sort_tag_wins) {
    ASSERT_TRUE(lib_collate_cmp("Weird Al", "Yankovic, Weird Al", "Zed", NULL) < 0);
    ASSERT_TRUE(lib_collate_cmp("Weird Al", "Yankovic, Weird Al", "Yardbirds", NULL) < 0);
    ASSERT_TRUE(lib_collate_cmp("Weird Al", "", "Zed", NULL) < 0);
    ASSERT_TRUE(lib_collate_cmp("Abba", "Zzz", "Beatles", NULL) > 0); /* the tag decides, not the name */
    ASSERT_EQ(0, lib_collate_cmp("x", "The Y", "y", NULL));           /* "The " is also dropped from a sort tag */
}

TEST(collate_buckets) {
    uint8_t k[LIB_KEY_MAX];
    lib_collate_key("Abba", NULL, k, sizeof k);
    ASSERT_EQ(1, lib_collate_bucket(k));
    lib_collate_key("The Zombies", NULL, k, sizeof k);
    ASSERT_EQ(26, lib_collate_bucket(k));
    lib_collate_key("\xC3\x89mile", NULL, k, sizeof k);
    ASSERT_EQ(5, lib_collate_bucket(k)); /* E */
    lib_collate_key("10cc", NULL, k, sizeof k);
    ASSERT_EQ(0, lib_collate_bucket(k));
    lib_collate_key("!!!", NULL, k, sizeof k);
    ASSERT_EQ(0, lib_collate_bucket(k));
    lib_collate_key("\xCE\xB1", NULL, k, sizeof k);
    ASSERT_EQ(0, lib_collate_bucket(k));
    /* a long name is cut at the cap but never overruns it */
    char longname[300];
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    size_t n = lib_collate_key(longname, NULL, k, 40);
    ASSERT_EQ(40, n);
}

/* ---- genre references ---------------------------------------------------------------------------------------------- */

static const char *genre(const char *in, bool bare) {
    static char out[40];
    meta_genre_text(out, sizeof out, in, bare);
    return out;
}

TEST(genre_references) {
    ASSERT_EQ(0, strcmp("Rock", genre("(17)", false)));
    ASSERT_EQ(0, strcmp("Pop", genre("(13)", false)));
    ASSERT_EQ(0, strcmp("Blues", genre("(0)", false)));
    ASSERT_EQ(0, strcmp("Synthpop", genre("(147)", false)));    /* the last Winamp extension */
    ASSERT_EQ(0, strcmp("", genre("(148)", false)));            /* not a genre */
    ASSERT_EQ(0, strcmp("Hard Rock", genre("(79)", false)));
    ASSERT_EQ(0, strcmp("Folk", genre("(80)", false)));
    ASSERT_EQ(0, strcmp("Alt Rock", genre("(20)Alt Rock", false))); /* the refinement text wins */
    ASSERT_EQ(0, strcmp("Rock", genre("(17)(18)", false)));      /* several: the first */
    ASSERT_EQ(0, strcmp("(Live)", genre("((Live)", false)));     /* "((" is a literal "(" */
    ASSERT_EQ(0, strcmp("Rock", genre("17", true)));             /* v2.4 plain number */
    ASSERT_EQ(0, strcmp("17", genre("17", false)));              /* not in Vorbis / MP4 text */
    ASSERT_EQ(0, strcmp("1990s", genre("1990s", true)));
    ASSERT_EQ(0, strcmp("Trip Hop", genre("Trip Hop", true)));
    ASSERT_EQ(0, strcmp("", genre("(RX)", false)));
    ASSERT_EQ(0, strcmp("", genre("(RX)(8)", false)));          /* Remix / Cover carry no genre */
    char small[5];
    meta_genre_text(small, sizeof small, "(17)Caf\xC3\xA9 Rock", false); /* cut at a character boundary */
    ASSERT_EQ(0, strcmp("Caf", small));
}

/* ---- tags per format ----------------------------------------------------------------------------------------------- */

typedef struct {
    const uint8_t *d;
    size_t n;
} mem_t;

static int32_t mem_rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    const mem_t *m = ctx;
    if (off >= m->n) return 0;
    if (n > m->n - off) n = (uint32_t)(m->n - off);
    memcpy(dst, m->d + off, n);
    return (int32_t)n;
}

static uint8_t g_buf[8192];
static uint8_t g_tagscratch[4096];

static const fx_tags_t k_full = {.title = "Song", .artist = "Perf", .album = "Alb", .aartist = "Album Perf", .genre = "(17)",
                                 .s_title = "Song, The", .s_artist = "Perf, The", .s_album = "Alb, An", .s_aartist = "AlbPerf",
                                 .track = 7, .disc = 2, .year = 2019, .comp = true};

static void check_new_fields(const audio_tags_t *t, const char *genre_expected) {
    ASSERT_EQ(0, strcmp(t->title, "Song"));
    ASSERT_EQ(0, strcmp(t->artist, "Perf"));
    ASSERT_EQ(0, strcmp(t->album, "Alb"));
    ASSERT_EQ(0, strcmp(t->album_artist, "Album Perf"));
    ASSERT_EQ(0, strcmp(t->genre, genre_expected));
    ASSERT_EQ(0, strcmp(t->sort_title, "Song, The"));
    ASSERT_EQ(0, strcmp(t->sort_artist, "Perf, The"));
    ASSERT_EQ(0, strcmp(t->sort_album, "Alb, An"));
    ASSERT_EQ(0, strcmp(t->sort_album_artist, "AlbPerf"));
    ASSERT_EQ(7, t->track);
    ASSERT_EQ(2, t->disc);
    ASSERT_EQ(2019, t->year);
    ASSERT_TRUE(t->compilation);
}

TEST(tags_flac) {
    meta_set_scratch(g_tagscratch, sizeof g_tagscratch);
    fx_tags_t t = k_full;
    t.genre = "Rock"; /* Vorbis genres are text */
    mem_t m = {g_buf, fx_flac(g_buf, &t, 200)};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, m.n, &fi));
    check_new_fields(&fi.tags, "Rock");
    t.comp = false;
    t.disc = 0;
    t.year = 0;
    t.aartist = "";
    m.n = fx_flac(g_buf, &t, 200);
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, m.n, &fi)); /* absent: empty / zero / false */
    ASSERT_EQ(0, fi.tags.album_artist[0]);
    ASSERT_EQ(0, fi.tags.disc);
    ASSERT_EQ(0, fi.tags.year);
    ASSERT_FALSE(fi.tags.compilation);
    meta_set_scratch(NULL, 0);
}

TEST(tags_flac_alt_keys) {
    /* "ALBUM ARTIST", "DISC", "YEAR" and a "DATE" with a full date, and a lower-case key */
    uint8_t vc[256];
    size_t v = 0, at;
    fx_le32(vc, 0);
    v = 4;
    at = v;
    v += 4;
    v += fx_vc(vc + v, "album artist", "AA");
    v += fx_vc(vc + v, "DISC", "3/4");
    v += fx_vc(vc + v, "DATE", "2001-02-03");
    fx_le32(vc + at, 3);
    uint8_t f[512];
    size_t n = fx_flac(f, &(fx_tags_t){0}, 10); /* a valid file, then replace its comment block */
    size_t vc_at = 4 + 4 + 34;
    f[vc_at] = 0x80 | 4;
    f[vc_at + 1] = (uint8_t)(v >> 16);
    f[vc_at + 2] = (uint8_t)(v >> 8);
    f[vc_at + 3] = (uint8_t)v;
    memcpy(f + vc_at + 4, vc, v);
    n = vc_at + 4 + v;
    meta_set_scratch(g_tagscratch, sizeof g_tagscratch);
    mem_t m = {f, n};
    flac_info_t fi;
    ASSERT_EQ(META_OK, flac_parse(mem_rd, &m, n, &fi));
    ASSERT_EQ(0, strcmp(fi.tags.album_artist, "AA"));
    ASSERT_EQ(3, fi.tags.disc);
    ASSERT_EQ(2001, fi.tags.year);
    meta_set_scratch(NULL, 0);
}

TEST(tags_mp3_v23_and_v24) {
    for (int v24 = 0; v24 < 2; v24++) {
        mem_t m = {g_buf, fx_mp3(g_buf, &k_full, v24 != 0)};
        mp3_info_t mi;
        ASSERT_EQ(META_OK, mp3_parse(mem_rd, &m, m.n, &mi));
        check_new_fields(&mi.tags, "Rock"); /* TCON "(17)" is the ID3v1 reference for Rock */
    }
    fx_tags_t t = k_full;
    t.genre = "Shoegaze";
    mem_t m = {g_buf, fx_mp3(g_buf, &t, false)};
    mp3_info_t mi;
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &m, m.n, &mi));
    ASSERT_EQ(0, strcmp(mi.tags.genre, "Shoegaze"));
    t.genre = "17"; /* v2.4 allows a bare number */
    m.n = fx_mp3(g_buf, &t, true);
    ASSERT_EQ(META_OK, mp3_parse(mem_rd, &m, m.n, &mi));
    ASSERT_EQ(0, strcmp(mi.tags.genre, "Rock"));
}

TEST(tags_mp4_ilst) {
    mem_t m = {g_buf, fx_m4a_aac(g_buf, &k_full)};
    mp4_info_t mi;
    ASSERT_EQ(META_E_AAC, mp4_parse(mem_rd, &m, m.n, &mi)); /* tags are read even for a file the player cannot decode */
    check_new_fields(&mi.tags, "Rock"); /* the fixture writes "(17)" as the (c)gen text */
}

TEST(tags_mp4_gnre_and_literal_genre) {
    /* gnre is the ID3v1 number plus one */
    uint8_t ilst[128], ib[160], meta[200], udta[240], moov[300], f[400];
    size_t n = 0;
    uint8_t v[2] = {0, 18}; /* 17 + 1 = Rock */
    n += fx_ilst_data(ilst + n, "gnre", v, 2);
    size_t k = fx_box(ib, "ilst", ilst, n);
    memset(meta, 0, 4);
    memcpy(meta + 4, ib, k);
    uint8_t mb[220];
    size_t mk = fx_box(mb, "meta", meta, 4 + k);
    size_t uk = fx_box(udta, "udta", mb, mk);
    size_t ok = fx_box(moov, "moov", udta, uk);
    uint8_t ftyp[8] = {'M', '4', 'A', ' ', 0, 0, 0, 0};
    size_t at = fx_box(f, "ftyp", ftyp, 8);
    memcpy(f + at, moov, ok);
    at += ok;
    mem_t m = {f, at};
    mp4_info_t mi;
    mp4_parse(mem_rd, &m, m.n, &mi);
    ASSERT_EQ(0, strcmp(mi.tags.genre, "Rock"));
}

/* ---- a library on a disk image ------------------------------------------------------------------------------------- */

static uint8_t g_file[16384];
static uint8_t g_scratch[LIB_SCRATCH_MIN] __attribute__((aligned(8)));

typedef enum { F_FLAC, F_MP3, F_WAV, F_AAC } fx_kind_t;

static bool put(const char *path, fx_kind_t k, const fx_tags_t *t) {
    size_t n = 0;
    switch (k) {
    case F_FLAC: n = fx_flac(g_file, t, 200); break;
    case F_MP3: n = fx_mp3(g_file, t, false); break;
    case F_WAV: n = fx_wav(g_file, 100); break;
    case F_AAC: n = fx_m4a_aac(g_file, t); break;
    }
    return fx_put(path, g_file, n);
}

/** The curated tree. @p extra adds one more album (crash tests tell the new library from the old by its track count). */
static bool make_tree(bool extra) {
    bool ok = true;
#define T(...) &(fx_tags_t){__VA_ARGS__}
    ok &= put("/Music/The Band/Big Pink/01 Tears.flac", F_FLAC, T(.title = "Tears of Rage", .artist = "The Band", .album = "Music from Big Pink", .genre = "Rock", .track = 1, .disc = 1, .year = 1968));
    ok &= put("/Music/The Band/Big Pink/02 Kingdom.flac", F_FLAC, T(.title = "To Kingdom Come", .artist = "The Band", .album = "Music from Big Pink", .genre = "Rock", .track = 2, .disc = 1, .year = 1968));
    /* file order is not track order */
    ok &= put("/Music/Abba/Gold/b.mp3", F_MP3, T(.title = "Mamma Mia", .artist = "ABBA", .album = "Gold", .genre = "(13)", .track = 2, .disc = 1, .year = 1976));
    ok &= put("/Music/Abba/Gold/a.mp3", F_MP3, T(.title = "Dancing Queen", .artist = "ABBA", .album = "Gold", .genre = "(13)", .track = 1, .disc = 1, .year = 1976));
    ok &= put("/Music/Abba/Gold/cover.jpg", F_WAV, T(0)); /* not audio: by its name */
    ok &= put("/Music/Abba/._a.mp3", F_MP3, T(.title = "AppleDouble junk"));
    ok &= put("/Music/.hidden/x.flac", F_FLAC, T(.title = "Hidden"));
    ok &= put("/Music/readme.txt", F_WAV, T(0));
    /* two albums called Greatest Hits stay apart; one album over two folders stays one */
    ok &= put("/Music/Emile/Greatest Hits/01.flac", F_FLAC, T(.title = "Z\xC3\xA8" "bre", .artist = "\xC3\x89mile Zola", .album = "Greatest Hits", .track = 1));
    ok &= put("/Music/Zed/Greatest Hits/01.flac", F_FLAC, T(.title = "Zulu", .artist = "Zed", .album = "Greatest Hits", .track = 1, .disc = 1));
    ok &= put("/Music/Zed/Greatest Hits/02.flac", F_FLAC, T(.title = "Zebra", .artist = "Zed", .album = "Greatest Hits", .track = 2, .disc = 1));
    ok &= put("/Music/Zed/Greatest Hits CD2/01.flac", F_FLAC, T(.title = "Zinc", .artist = "Zed", .album = "Greatest Hits", .track = 1, .disc = 2));
    /* compilations: the flag, and "Various Artists" */
    ok &= put("/Music/Mix/Now/01.flac", F_FLAC, T(.title = "Hit A", .artist = "A Band", .album = "Now 99", .track = 1, .comp = true));
    ok &= put("/Music/Mix/Now/02.flac", F_FLAC, T(.title = "Hit B", .artist = "Zed Zed", .album = "Now 99", .track = 2, .comp = true));
    ok &= put("/Music/Mix/Various/01.flac", F_FLAC, T(.title = "Hit C", .artist = "Q", .album = "Hits Vol 1", .aartist = "Various Artists", .track = 1));
    /* no tags: title is the file name, "Unknown Artist", album is the folder; and no title / no album with an artist */
    ok &= put("/Music/Loose/song.wav", F_WAV, T(0));
    ok &= put("/Music/Loose/notitle.flac", F_FLAC, T(.artist = "Quux"));
    /* a file that cannot be played is listed all the same */
    ok &= put("/Music/Aac/a.m4a", F_AAC, T(.title = "Aac Song", .artist = "Aaa Artist", .album = "Aac Album", .genre = "Pop", .track = 1));
    /* sort tag, digits, symbols */
    ok &= put("/Music/Sort/x.flac", F_FLAC, T(.title = "Eat It", .artist = "Weird Al", .s_artist = "Yankovic, Weird Al", .genre = "Parody"));
    ok &= put("/Music/Bang/01.flac", F_FLAC, T(.title = "Me and Giuliani", .artist = "!!!", .album = "Louden Up Now", .genre = "Punk"));
    ok &= put("/Music/Ten/01.flac", F_FLAC, T(.title = "I'm Not in Love", .artist = "10cc", .album = "Sheet Music", .genre = "Rock"));
    if (extra) ok &= put("/Music/Extra/01.flac", F_FLAC, T(.title = "Extra", .artist = "Extra", .album = "Extra"));
#undef T
    return ok;
}

static lib_err_t build(int stop, lib_build_stats_t *st) {
    lib_build_cfg_t cfg = {.fingerprint = 0x1122334455667788ull, .test_stop_step = stop};
    return lib_build(&cfg, g_scratch, sizeof g_scratch, st);
}

static lib_reader_t g_rd;

static bool lib_exists(const char *path) {
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

static uint32_t find_artist(const char *name) {
    for (uint32_t i = 0; i < lib_manifest(&g_rd)->artists; i++) {
        lib_entity_t e;
        if (lib_artist(&g_rd, i, &e) == LIB_OK && strcmp(e.name, name) == 0) return i;
    }
    return LIB_NONE;
}

static bool find_track(const char *title, lib_track_t *out) {
    for (uint32_t i = 0; i < lib_manifest(&g_rd)->tracks; i++) {
        if (lib_track(&g_rd, i, out) == LIB_OK && strcmp(out->title, title) == 0) return true;
    }
    return false;
}

TEST(build_and_browse) {
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    ASSERT_TRUE(make_tree(false));
    lib_build_stats_t st;
    ASSERT_EQ(LIB_OK, build(0, &st));
    ASSERT_EQ(17, st.tracks);
    ASSERT_EQ(16, st.playable);
    ASSERT_EQ(10, st.artists);
    ASSERT_EQ(12, st.albums);
    ASSERT_EQ(1, st.generation);
    ASSERT_TRUE(st.scratch_peak <= LIB_SCRATCH_MIN);

    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    const lib_manifest_t *m = lib_manifest(&g_rd);
    ASSERT_EQ(17, m->tracks);
    ASSERT_EQ(16, m->playable);
    ASSERT_EQ(0x1122334455667788ull, m->fingerprint);
    ASSERT_EQ(LIB_DECODER_SET_VERSION, m->decoder_version);
    ASSERT_EQ(10, lib_list_count(&g_rd, LIB_LIST_ARTISTS));
    ASSERT_EQ(12, lib_list_count(&g_rd, LIB_LIST_ALBUMS));
    ASSERT_EQ(2, lib_list_count(&g_rd, LIB_LIST_COMPILATIONS));
    ASSERT_EQ(17, lib_list_count(&g_rd, LIB_LIST_SONGS));

    /* Artists: album artists only (no compilation performers), symbols < digits < letters, "The" ignored, accents folded, sort tag wins */
    static const char *const artists[10] = {"!!!", "10cc", "Aaa Artist", "ABBA", "The Band", "\xC3\x89mile Zola", "Quux", "Unknown Artist", "Weird Al", "Zed"};
    for (uint32_t i = 0; i < 10; i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_ARTISTS, i, &r));
        ASSERT_EQ(0, strcmp(r.text, artists[i]));
    }
    ASSERT_EQ(LIB_NONE, find_artist("A Band")); /* performers on a compilation are not album artists */
    ASSERT_EQ(LIB_NONE, find_artist("Various Artists"));

    /* artist -> albums -> tracks, as ranges */
    uint32_t zed = find_artist("Zed");
    lib_entity_t ar, al;
    ASSERT_EQ(LIB_OK, lib_artist(&g_rd, zed, &ar));
    ASSERT_EQ(1, ar.count); /* both folders are one "Greatest Hits" */
    ASSERT_EQ(LIB_OK, lib_album(&g_rd, ar.first, &al));
    ASSERT_EQ(0, strcmp(al.name, "Greatest Hits"));
    ASSERT_EQ(3, al.count);
    ASSERT_EQ(zed, al.artist);
    static const char *const zed_order[3] = {"Zulu", "Zebra", "Zinc"}; /* disc 1 track 1, disc 1 track 2, disc 2 track 1 */
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t tid;
        lib_track_t t;
        ASSERT_EQ(LIB_OK, lib_by_album_at(&g_rd, al.first + i, &tid));
        ASSERT_EQ(LIB_OK, lib_track(&g_rd, tid, &t));
        ASSERT_EQ(0, strcmp(t.title, zed_order[i]));
        ASSERT_EQ(zed, t.artist);
        ASSERT_EQ(ar.first, t.album);
    }
    uint32_t emile = find_artist("\xC3\x89mile Zola");
    ASSERT_EQ(LIB_OK, lib_artist(&g_rd, emile, &ar));
    ASSERT_EQ(LIB_OK, lib_album(&g_rd, ar.first, &al));
    ASSERT_EQ(1, al.count); /* the other Greatest Hits */

    /* Gold: tracks by track number, not file order; genre from "(13)"; year, disc */
    uint32_t abba = find_artist("ABBA");
    ASSERT_EQ(LIB_OK, lib_artist(&g_rd, abba, &ar));
    ASSERT_EQ(LIB_OK, lib_album(&g_rd, ar.first, &al));
    ASSERT_EQ(0, strcmp(al.name, "Gold"));
    uint32_t t0, t1;
    lib_track_t ta, tb;
    ASSERT_EQ(LIB_OK, lib_by_album_at(&g_rd, al.first, &t0));
    ASSERT_EQ(LIB_OK, lib_by_album_at(&g_rd, al.first + 1, &t1));
    ASSERT_EQ(LIB_OK, lib_track(&g_rd, t0, &ta));
    ASSERT_EQ(LIB_OK, lib_track(&g_rd, t1, &tb));
    ASSERT_EQ(0, strcmp(ta.title, "Dancing Queen"));
    ASSERT_EQ(0, strcmp(tb.title, "Mamma Mia"));
    ASSERT_EQ(1976, ta.year);
    ASSERT_EQ(1, ta.disc);
    ASSERT_EQ(1, ta.track);
    ASSERT_EQ(LIB_PLAY_OK, ta.play);
    ASSERT_EQ(LIB_CODEC_MP3, ta.codec);
    ASSERT_EQ(44100, ta.rate);
    char path[128];
    ASSERT_EQ(LIB_OK, lib_track_path(&g_rd, t0, path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Music/Abba/Gold/a.mp3"));
    ASSERT_EQ(LIB_E_RANGE, lib_track_path(&g_rd, t0, path, 10));

    /* Compilations: the flag and "Various Artists", each its own album under Compilations */
    lib_row_t r;
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_COMPILATIONS, 0, &r));
    ASSERT_EQ(0, strcmp(r.text, "Hits Vol 1"));
    ASSERT_EQ(1, r.count);
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_COMPILATIONS, 1, &r));
    ASSERT_EQ(0, strcmp(r.text, "Now 99"));
    ASSERT_EQ(2, r.count);
    lib_track_t t;
    ASSERT_TRUE(find_track("Hit B", &t));
    ASSERT_TRUE(t.compilation);
    ASSERT_EQ(LIB_NONE, t.artist);
    ASSERT_EQ(0, strcmp(t.artist_name, "Zed Zed")); /* the track still shows its own performer */

    /* Fallbacks: file name as title, Unknown Artist, folder name as album */
    ASSERT_TRUE(find_track("song.wav", &t));
    ASSERT_EQ(0, strcmp(t.artist_name, "Unknown Artist"));
    ASSERT_EQ(LIB_CODEC_WAV, t.codec);
    ASSERT_EQ(LIB_PLAY_OK, t.play);
    ASSERT_EQ(LIB_OK, lib_album(&g_rd, t.album, &al));
    ASSERT_EQ(0, strcmp(al.name, "Loose"));
    uint32_t unknown = find_artist("Unknown Artist");
    ASSERT_EQ(unknown, t.artist);
    ASSERT_TRUE(find_track("notitle.flac", &t));
    ASSERT_EQ(0, strcmp(t.artist_name, "Quux"));
    lib_entity_t al2;
    ASSERT_EQ(LIB_OK, lib_album(&g_rd, t.album, &al2));
    ASSERT_EQ(0, strcmp(al2.name, "Loose"));
    ASSERT_TRUE(al2.artist != al.artist); /* the same album name under another artist is another album */

    /* D6: AAC is listed, flagged, with its tags */
    ASSERT_TRUE(find_track("Aac Song", &t));
    ASSERT_EQ(LIB_PLAY_CODEC, t.play);
    ASSERT_EQ(LIB_CODEC_AAC, t.codec);
    ASSERT_EQ(0, strcmp(t.artist_name, "Aaa Artist"));
    ASSERT_FALSE(find_track("AppleDouble junk", &t));
    ASSERT_FALSE(find_track("Hidden", &t));

    /* Sort tag: "Weird Al" files under Y; the track keeps the name */
    ASSERT_TRUE(find_track("Eat It", &t));
    ASSERT_EQ(0, strcmp(t.artist_name, "Weird Al"));

    /* Songs: sorted by title key; every track exactly once */
    uint8_t seen[32] = {0};
    char prev[LIB_TEXT_MAX] = "";
    for (uint32_t i = 0; i < 17; i++) {
        lib_row_t sr;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &sr));
        ASSERT_TRUE(lib_collate_cmp(prev, NULL, sr.text, NULL) <= 0);
        snprintf(prev, sizeof prev, "%s", sr.text);
        ASSERT_TRUE(sr.id < 17 && !seen[sr.id]);
        seen[sr.id] = 1;
    }
    lib_row_t first;
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, 0, &first));
    ASSERT_EQ(0, strcmp(first.text, "Aac Song"));
    ASSERT_EQ(LIB_PLAY_CODEC, first.play);

    /* Albums by name */
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_ALBUMS, 0, &r));
    ASSERT_EQ(0, strcmp(r.text, "Aac Album"));
    ASSERT_EQ(0, strcmp(r.sub, "Aaa Artist"));

    /* Genres and the genre -> artists links; compilations do not link */
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_GENRES, 0, &r));
    uint32_t ng = lib_list_count(&g_rd, LIB_LIST_GENRES);
    ASSERT_EQ(4, ng); /* Parody, Pop, Punk, Rock */
    static const char *const genres[4] = {"Parody", "Pop", "Punk", "Rock"};
    static const uint32_t gartists[4] = {1, 2, 1, 2}; /* Pop: ABBA + Aaa Artist; Rock: The Band + 10cc */
    for (uint32_t i = 0; i < ng; i++) {
        lib_entity_t g;
        ASSERT_EQ(LIB_OK, lib_genre(&g_rd, i, &g));
        ASSERT_EQ(0, strcmp(g.name, genres[i]));
        ASSERT_EQ(gartists[i], g.count);
    }
    lib_entity_t g;
    ASSERT_EQ(LIB_OK, lib_genre(&g_rd, 3, &g));
    uint32_t a0, a1;
    ASSERT_EQ(LIB_OK, lib_genre_artist_at(&g_rd, g.first, &a0));
    ASSERT_EQ(LIB_OK, lib_genre_artist_at(&g_rd, g.first + 1, &a1));
    ASSERT_TRUE(a0 < a1);
    ASSERT_EQ(find_artist("10cc"), a0);
    ASSERT_EQ(find_artist("The Band"), a1);
    ASSERT_EQ(6, lib_manifest(&g_rd)->genre_artist_links);

    /* Letter jumps: the first row at or after the letter */
    ASSERT_EQ(0, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, '#'));
    ASSERT_EQ(2, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'A'));     /* Aaa Artist (after "!!!" and "10cc") */
    ASSERT_EQ(4, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'b'));     /* The Band */
    ASSERT_EQ(5, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'C'));     /* no C or D: the next letter present, E (Emile) */
    ASSERT_EQ(5, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'E'));
    ASSERT_EQ(6, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'q'));
    ASSERT_EQ(8, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'Y'));     /* Weird Al files under Y */
    ASSERT_EQ(9, lib_list_jump(&g_rd, LIB_LIST_ARTISTS, 'Z'));
    for (unsigned b = 1; b < LIB_BUCKETS; b++) ASSERT_TRUE(m->jump[LIB_LIST_ARTISTS][b - 1] <= m->jump[LIB_LIST_ARTISTS][b]);

    /* Page cache: 4 pages, hits and misses are counted */
    ASSERT_TRUE(g_rd.page_misses > 0);
    uint32_t misses = g_rd.page_misses;
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, 3, &r));
    ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, 3, &r));
    ASSERT_TRUE(g_rd.page_misses - misses <= 4); /* the second read is all hits */

    /* bounds */
    ASSERT_EQ(LIB_E_RANGE, lib_list_row(&g_rd, LIB_LIST_ARTISTS, 10, &r));
    ASSERT_EQ(LIB_E_RANGE, lib_track(&g_rd, 17, &t));
    ASSERT_EQ(LIB_E_RANGE, lib_by_album_at(&g_rd, 17, &t0));
    lib_close(&g_rd);

    /* the whole set verifies, CRCs included */
    lib_manifest_t mm;
    ASSERT_EQ(LIB_OK, lib_manifest_load("/.neupod/lib", true, &mm));
    ASSERT_FALSE(lib_exists("/.neupod/lib.tmp"));
    ASSERT_FALSE(lib_exists("/.neupod/lib.old"));
    ASSERT_FALSE(lib_exists("/.neupod/work"));
}

TEST(rebuild_bumps_generation_and_empty_library) {
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    lib_build_stats_t st;
    ASSERT_EQ(LIB_OK, build(0, &st)); /* no /Music at all: an empty library, not an error */
    ASSERT_EQ(0, st.tracks);
    ASSERT_EQ(1, st.generation);
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    ASSERT_EQ(0, lib_list_count(&g_rd, LIB_LIST_ARTISTS));
    ASSERT_EQ(0, lib_list_count(&g_rd, LIB_LIST_SONGS));
    lib_row_t r;
    ASSERT_EQ(LIB_E_RANGE, lib_list_row(&g_rd, LIB_LIST_SONGS, 0, &r));
    lib_close(&g_rd);
    ASSERT_TRUE(make_tree(false));
    ASSERT_EQ(LIB_OK, build(0, &st));
    ASSERT_EQ(2, st.generation);
    ASSERT_EQ(17, st.tracks);
    ASSERT_EQ(LIB_E_SCRATCH, lib_build(&(lib_build_cfg_t){0}, g_scratch, 1024, NULL)); /* below the minimum: refused before any write */
}

/* ---- failure and crash safety -------------------------------------------------------------------------------------- */

static bool cancel_after_3(void *user, uint32_t done) {
    (void)user;
    return done < 3;
}

TEST(cancel_leaves_old_library) {
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    ASSERT_TRUE(make_tree(false));
    ASSERT_EQ(LIB_OK, build(0, NULL));
    ASSERT_TRUE(put("/Music/Extra/01.flac", F_FLAC, &(fx_tags_t){.title = "Extra"}));
    lib_build_cfg_t cfg = {.progress = cancel_after_3};
    ASSERT_EQ(LIB_E_CANCELLED, lib_build(&cfg, g_scratch, sizeof g_scratch, NULL));
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    ASSERT_EQ(17, lib_manifest(&g_rd)->tracks);
    ASSERT_EQ(1, lib_manifest(&g_rd)->generation);
    lib_close(&g_rd);
    ASSERT_FALSE(lib_exists("/.neupod/lib.tmp")); /* a cancelled build cleans up after itself */
    ASSERT_FALSE(lib_exists("/.neupod/work"));
}

// What a reader sees after recovery must be a whole library: either generation 1 (17 tracks) or generation 2 (18), consistent throughout.
static void expect_whole(uint32_t generation, uint32_t tracks) {
    lib_manifest_t mm;
    ASSERT_EQ(LIB_OK, lib_manifest_load("/.neupod/lib", true, &mm));
    ASSERT_EQ(generation, mm.generation);
    ASSERT_EQ(tracks, mm.tracks);
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    ASSERT_EQ(tracks, lib_list_count(&g_rd, LIB_LIST_SONGS));
    for (uint32_t i = 0; i < tracks; i++) { /* every row of every table resolves */
        lib_row_t r;
        lib_track_t t;
        char p[128];
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &r));
        ASSERT_EQ(LIB_OK, lib_track(&g_rd, i, &t));
        ASSERT_EQ(LIB_OK, lib_track_path(&g_rd, i, p, sizeof p));
        FILINFO fi;
        ASSERT_EQ(FR_OK, f_stat(p, &fi)); /* the paths exist: the index and the card agree */
    }
    lib_close(&g_rd);
}

TEST(swap_survives_power_cut_at_every_step) {
    /* generation 1 is on the card; then the card gets one more album and the rebuild is cut after step s */
    static const struct {
        int step;
        uint32_t gen, tracks; /* what boot recovery must leave */
    } cases[] = {{1, 1, 17}, {2, 1, 17}, {3, 1, 17}, {4, 2, 18}, {5, 2, 18}};
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        ASSERT_TRUE(fx_volume(16u << 20, 0));
        ASSERT_TRUE(make_tree(false));
        ASSERT_EQ(LIB_OK, build(0, NULL));
        ASSERT_TRUE(put("/Music/Extra/01.flac", F_FLAC, &(fx_tags_t){.title = "Extra", .artist = "Extra", .album = "Extra"}));
        ASSERT_EQ(LIB_E_TEST_STOP, build(cases[c].step, NULL));
        ASSERT_EQ(LIB_OK, lib_recover(NULL));
        expect_whole(cases[c].gen, cases[c].tracks);
        ASSERT_FALSE(lib_exists("/.neupod/lib.old"));
        ASSERT_FALSE(lib_exists("/.neupod/lib.tmp"));
        ASSERT_FALSE(lib_exists("/.neupod/work"));
        ASSERT_EQ(LIB_OK, lib_recover(NULL)); /* idempotent */
        expect_whole(cases[c].gen, cases[c].tracks);
    }
}

TEST(first_build_power_cut) {
    /* no old library: cut before the manifest -> nothing; cut with the new set complete -> the new set */
    static const struct {
        int step;
        lib_err_t rec;
    } cases[] = {{1, LIB_E_NONE}, {2, LIB_OK}, {4, LIB_OK}};
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        ASSERT_TRUE(fx_volume(16u << 20, 0));
        ASSERT_TRUE(make_tree(false));
        ASSERT_EQ(LIB_E_TEST_STOP, build(cases[c].step, NULL));
        ASSERT_EQ(cases[c].rec, lib_recover(NULL));
        if (cases[c].rec == LIB_OK) expect_whole(1, 17);
        else ASSERT_EQ(LIB_E_NONE, lib_open(&g_rd, NULL));
        ASSERT_FALSE(lib_exists("/.neupod/lib.tmp"));
    }
}

TEST(damaged_manifest_falls_back_to_old) {
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    ASSERT_TRUE(make_tree(false));
    ASSERT_EQ(LIB_OK, build(0, NULL));
    ASSERT_TRUE(put("/Music/Extra/01.flac", F_FLAC, &(fx_tags_t){.title = "Extra"}));
    ASSERT_EQ(LIB_E_TEST_STOP, build(4, NULL)); /* lib = gen 2, lib.old = gen 1 */
    /* flip a bit in the new manifest: it no longer checks out */
    FIL f;
    UINT n;
    uint8_t b;
    ASSERT_EQ(FR_OK, f_open(&f, "/.neupod/lib/manifest.bin", FA_READ | FA_WRITE));
    ASSERT_EQ(FR_OK, f_lseek(&f, 40));
    ASSERT_EQ(FR_OK, f_read(&f, &b, 1, &n));
    b ^= 0x40;
    ASSERT_EQ(FR_OK, f_lseek(&f, 40));
    ASSERT_EQ(FR_OK, f_write(&f, &b, 1, &n));
    ASSERT_EQ(FR_OK, f_close(&f));
    ASSERT_EQ(LIB_E_FORMAT, lib_open(&g_rd, NULL));
    ASSERT_EQ(LIB_OK, lib_recover(NULL));
    expect_whole(1, 17);
    /* a damaged table is found by the full check */
    ASSERT_EQ(FR_OK, f_open(&f, "/.neupod/lib/tracks.bin", FA_READ | FA_WRITE));
    ASSERT_EQ(FR_OK, f_lseek(&f, 100));
    ASSERT_EQ(FR_OK, f_read(&f, &b, 1, &n));
    b ^= 0x01;
    ASSERT_EQ(FR_OK, f_lseek(&f, 100));
    ASSERT_EQ(FR_OK, f_write(&f, &b, 1, &n));
    ASSERT_EQ(FR_OK, f_close(&f));
    lib_manifest_t mm;
    ASSERT_EQ(LIB_OK, lib_manifest_load("/.neupod/lib", false, &mm));
    ASSERT_EQ(LIB_E_FORMAT, lib_manifest_load("/.neupod/lib", true, &mm));
}

TEST(small_scratch_still_builds_with_more_merging) {
    /* the 32 KB minimum, with enough tracks that every sort needs several runs and a merge */
    ASSERT_TRUE(fx_volume(64u << 20, 0));
    for (int i = 0; i < 2000; i++) {
        char p[96];
        char title[40], artist[40], album[40];
        snprintf(p, sizeof p, "/Music/A%03d/Al%03d/t%04d.flac", i % 40, i % 97, i);
        snprintf(title, sizeof title, "Title %04d", (i * 7919) % 2000);
        snprintf(artist, sizeof artist, "Artist %02d", i % 40);
        snprintf(album, sizeof album, "Album %02d", i % 97);
        ASSERT_TRUE(put(p, F_FLAC, &(fx_tags_t){.title = title, .artist = artist, .album = album, .track = 1 + i % 12}));
    }
    lib_build_stats_t st;
    ASSERT_EQ(LIB_OK, build(0, &st));
    ASSERT_EQ(2000, st.tracks);
    ASSERT_EQ(40, st.artists);
    ASSERT_TRUE(st.sort_runs > 4);
    ASSERT_TRUE(st.sort_merges >= 1);
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    char prev[LIB_TEXT_MAX] = "";
    for (uint32_t i = 0; i < 2000; i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &r));
        ASSERT_TRUE(strcmp(prev, r.text) <= 0);
        snprintf(prev, sizeof prev, "%s", r.text);
    }
    lib_close(&g_rd);
}

int main(void) {
    RUN(collate_case_and_the);
    RUN(collate_diacritics);
    RUN(collate_classes);
    RUN(collate_sort_tag_wins);
    RUN(collate_buckets);
    RUN(genre_references);
    RUN(tags_flac);
    RUN(tags_flac_alt_keys);
    RUN(tags_mp3_v23_and_v24);
    RUN(tags_mp4_ilst);
    RUN(tags_mp4_gnre_and_literal_genre);
    RUN(build_and_browse);
    RUN(rebuild_bumps_generation_and_empty_library);
    RUN(cancel_leaves_old_library);
    RUN(swap_survives_power_cut_at_every_step);
    RUN(first_build_power_cut);
    RUN(damaged_manifest_falls_back_to_old);
    RUN(small_scratch_still_builds_with_more_merging);
    fx_cleanup();
    return RESULT();
}
