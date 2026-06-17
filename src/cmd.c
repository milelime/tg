/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/* cmd.c - subcommands, layered over tagset (set logic) and xattr (storage). */
#define _XOPEN_SOURCE 700 /* nftw */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cmd.h"
#include "tagset.h"
#include "xattr.h"

#define TAG_BUF_CAP 4096

static const char *xattr_strerror(TgXattrResult r)
{
    switch (r) {
    case TG_XATTR_OK:
        return "ok";
    case TG_XATTR_ABSENT:
        return "no tags";
    case TG_XATTR_UNSUPPORTED:
        return "xattrs not supported on this filesystem";
    case TG_XATTR_NOFILE:
        return "no such file";
    case TG_XATTR_ACCESS:
        return "permission denied";
    case TG_XATTR_NOSPACE:
        return "no space left on device";
    case TG_XATTR_TOOBIG:
        return "tag value too large";
    case TG_XATTR_ERROR:
        return "error";
    }
    return "error";
}

/* Read a file's tags into a fresh ts. A missing attribute yields an empty set
 * (success). ts is always initialized, so the caller can always tg_tagset_free. */
static bool load_tags(const char *path, TgTagSet *ts)
{
    tg_tagset_init(ts);

    char buf[TAG_BUF_CAP];
    size_t len = 0;
    TgXattrResult r = tg_xattr_get(path, buf, sizeof buf, &len);

    if (r == TG_XATTR_OK) {
        return tg_tagset_parse(ts, buf, len);
    }
    if (r == TG_XATTR_ABSENT) {
        return true;
    }

    fprintf(stderr, "tg: %s: %s\n", path, xattr_strerror(r));
    return false;
}

/* Write ts back, or remove the attribute when the set is empty. */
static bool store_tags(const char *path, const TgTagSet *ts)
{
    if (ts->count == 0) {
        TgXattrResult r = tg_xattr_remove(path);
        if (r != TG_XATTR_OK) {
            fprintf(stderr, "tg: %s: %s\n", path, xattr_strerror(r));
            return false;
        }
        return true;
    }

    size_t len = 0;
    char *value = tg_tagset_render(ts, &len);
    if (value == NULL) {
        fprintf(stderr, "tg: %s: render failed\n", path);
        return false;
    }

    TgXattrResult r = tg_xattr_set(path, value, len);
    free(value);
    if (r != TG_XATTR_OK) {
        fprintf(stderr, "tg: %s: %s\n", path, xattr_strerror(r));
        return false;
    }
    return true;
}

int cmd_add(const char *spec, int nfiles, char **files)
{
    int failed = 0;
    for (int i = 0; i < nfiles; i++) {
        TgTagSet ts;
        if (!load_tags(files[i], &ts)) {
            failed = 1;
            tg_tagset_free(&ts);
            continue;
        }
        tg_tagset_parse(&ts, spec, strlen(spec)); /* union new tags into existing */
        if (!store_tags(files[i], &ts)) {
            failed = 1;
        }
        tg_tagset_free(&ts);
    }
    return failed ? 2 : 0;
}

int cmd_rm(const char *spec, int nfiles, char **files)
{
    TgTagSet want;
    tg_tagset_init(&want);
    tg_tagset_parse(&want, spec, strlen(spec));

    int failed = 0;
    for (int i = 0; i < nfiles; i++) {
        TgTagSet ts;
        if (!load_tags(files[i], &ts)) {
            failed = 1;
            tg_tagset_free(&ts);
            continue;
        }
        for (size_t j = 0; j < want.count; j++) {
            tg_tagset_remove(&ts, want.items[j]);
        }
        if (!store_tags(files[i], &ts)) {
            failed = 1;
        }
        tg_tagset_free(&ts);
    }

    tg_tagset_free(&want);
    return failed ? 2 : 0;
}

int cmd_tags(int nfiles, char **files)
{
    int failed = 0;
    for (int i = 0; i < nfiles; i++) {
        TgTagSet ts;
        if (!load_tags(files[i], &ts)) {
            failed = 1;
            tg_tagset_free(&ts);
            continue;
        }
        if (ts.count == 0) {
            printf("%s\t\n", files[i]);
        } else {
            size_t len = 0;
            char *value = tg_tagset_render(&ts, &len);
            if (value == NULL) {
                failed = 1;
            } else {
                printf("%s\t%s\n", files[i], value);
                free(value);
            }
        }
        tg_tagset_free(&ts);
    }
    return failed ? 2 : 0;
}

int cmd_clear(int nfiles, char **files)
{
    int failed = 0;
    for (int i = 0; i < nfiles; i++) {
        TgXattrResult r = tg_xattr_remove(files[i]);
        if (r != TG_XATTR_OK) {
            fprintf(stderr, "tg: %s: %s\n", files[i], xattr_strerror(r));
            failed = 1;
        }
    }
    return failed ? 2 : 0;
}

/* cp mirrors src's tags onto dst, clearing dst when src has none. */
int cmd_cp(const char *src, const char *dst)
{
    TgTagSet ts;
    if (!load_tags(src, &ts)) {
        tg_tagset_free(&ts);
        return 2;
    }
    bool ok = store_tags(dst, &ts);
    tg_tagset_free(&ts);
    return ok ? 0 : 2;
}

#define COL_PATH "\033[36m"
#define COL_RESET "\033[0m"

/* nftw's callback has no user-data argument, so find's query and options pass
 * through these file-scope values. Safe because the CLI is single-threaded. */
static const TgTagSet *g_query;
static bool g_match_all;  /* match every query tag vs. any */
static bool g_null_delim; /* separate paths with '\0' vs. '\n' */
static bool g_color;

static bool has_all(const TgTagSet *file, const TgTagSet *query)
{
    for (size_t i = 0; i < query->count; i++) {
        if (!tg_tagset_has(file, query->items[i])) {
            return false;
        }
    }
    return true;
}

static bool has_any(const TgTagSet *file, const TgTagSet *query)
{
    for (size_t i = 0; i < query->count; i++) {
        if (tg_tagset_has(file, query->items[i])) {
            return true;
        }
    }
    return false;
}

static int find_cb(const char *fpath, const struct stat *sb, int typeflag,
                   struct FTW *ftw)
{
    (void)sb;
    (void)ftw;

    if (typeflag != FTW_F) {
        return 0;
    }

    /* Read quietly: a tree walk must not report unreadable or untagged files. */
    char buf[TAG_BUF_CAP];
    size_t len = 0;
    if (tg_xattr_get(fpath, buf, sizeof buf, &len) != TG_XATTR_OK) {
        return 0;
    }

    TgTagSet ts;
    tg_tagset_init(&ts);
    if (tg_tagset_parse(&ts, buf, len)) {
        bool ok = g_query->count == 0 || /* no query: match any tagged file */
                  (g_match_all ? has_all(&ts, g_query) : has_any(&ts, g_query));
        if (ok) {
            if (g_color) {
                fputs(COL_PATH, stdout);
                fputs(fpath, stdout);
                fputs(COL_RESET, stdout);
            } else {
                fputs(fpath, stdout);
            }
            putchar(g_null_delim ? '\0' : '\n');
        }
    }
    tg_tagset_free(&ts);

    return 0;
}

int cmd_find(int argc, char **argv)
{
    bool match_all = true;
    bool null_delim = false;
    const char *pos[2] = {NULL, NULL};
    int npos = 0;
    bool after_ddash = false;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!after_ddash && strcmp(a, "--") == 0) {
            after_ddash = true;
        } else if (!after_ddash && a[0] == '-' && a[1] != '\0') {
            if (strcmp(a, "--all") == 0) {
                match_all = true;
            } else if (strcmp(a, "--any") == 0) {
                match_all = false;
            } else if (strcmp(a, "-0") == 0) {
                null_delim = true;
            } else {
                fprintf(stderr, "tg: find: unknown option '%s'\n", a);
                return 1;
            }
        } else if (npos < 2) {
            pos[npos++] = a;
        } else {
            npos++; /* overflow; reported below */
        }
    }

    if (npos > 2) {
        fprintf(stderr, "usage: tg find [--any|--all] [-0] [tags] [path]\n");
        return 1;
    }

    const char *spec = (npos >= 1) ? pos[0] : NULL;
    const char *root = (npos >= 2) ? pos[1] : ".";

    TgTagSet query;
    tg_tagset_init(&query);
    if (spec != NULL) {
        tg_tagset_parse(&query, spec, strlen(spec));
        if (query.count == 0) {
            fprintf(stderr, "tg: find: no valid tags in '%s'\n", spec);
            tg_tagset_free(&query);
            return 1;
        }
    }

    g_query = &query;
    g_match_all = match_all;
    g_null_delim = null_delim;
    g_color = !null_delim && isatty(STDOUT_FILENO) == 1;

    int rc = nftw(root, find_cb, 16, FTW_PHYS);

    g_query = NULL;
    tg_tagset_free(&query);

    if (rc < 0) {
        fprintf(stderr, "tg: %s: %s\n", root, strerror(errno));
        return 2;
    }
    return 0;
}
