/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/* tagset.c - tag set storage and comma-separated (de)serialization. */
#define _POSIX_C_SOURCE 200809L /* strndup */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "tagset.h"

#define MAX_TAGS_ELEMENTS 32 /* generous ceiling; one file rarely needs more */

void tg_tagset_init(TgTagSet *ts)
{
    ts->items = NULL;
    ts->count = 0;
    ts->cap = 0;
}

void tg_tagset_free(TgTagSet *ts)
{
    for (size_t i = 0; i < ts->count; i++) {
        free(ts->items[i]);
    }

    free(ts->items);

    ts->items = NULL;
    ts->count = 0;
    ts->cap = 0;
}

bool tg_tagset_parse(TgTagSet *ts, const char *value, size_t len)
{
    const char *p = value;
    const char *end = value + len;

    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;

        const char *start = p;
        const char *stop = tok_end;

        while (start < stop && (*start == ' ' || *start == '\t')) {
            start++;
        }

        while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) {
            stop--;
        }

        if (stop > start) {
            char *tok = strndup(start, (size_t)(stop - start));

            if (tok == NULL) {
                fprintf(stderr, "tg: out of memory\n");
                return false;
            }

            tg_tagset_add(ts, tok);
            free(tok);
        }

        p = comma ? comma + 1 : end;
    }

    return true;
}

static bool tagset_grow(TgTagSet *ts)
{
    size_t new_cap = ts->cap ? ts->cap * 2 : 4;

    if (new_cap > MAX_TAGS_ELEMENTS) {
        fprintf(stderr, "tg: too many tags (max %d)\n", MAX_TAGS_ELEMENTS);
        return false;
    }

    char **items = realloc(ts->items, new_cap * sizeof ts->items);

    if (items == NULL) {
        fprintf(stderr, "tg: out of memory\n");
        return false;
    }

    ts->cap = new_cap;
    ts->items = items;

    return true;
}

bool tg_tagset_add(TgTagSet *ts, const char *tag)
{
    if (tg_tagset_has(ts, tag)) {
        return false;
    }

    if (ts->count == ts->cap) {
        if (!tagset_grow(ts)) {
            return false;
        }
    }

    char *local_tag = strdup(tag);

    if (local_tag == NULL) {
        fprintf(stderr, "tg: out of memory\n");
        return false;
    }

    ts->items[ts->count] = local_tag;
    ts->count++;

    return true;
}

bool tg_tagset_remove(TgTagSet *ts, const char *tag)
{
    for (size_t i = 0; i < ts->count; ++i) {
        if (strcmp(ts->items[i], tag) == 0) {
            free(ts->items[i]);
            ts->items[i] = ts->items[ts->count - 1];
            ts->items[ts->count - 1] = NULL;
            ts->count--;
            return true;
        }
    }

    return false;
}

bool tg_tagset_has(const TgTagSet *ts, const char *tag)
{
    for (size_t i = 0; i < ts->count; ++i) {
        if (strcmp(ts->items[i], tag) == 0) {
            return true;
        }
    }

    return false;
}

/* qsort passes pointers to the array elements, i.e. char ** here. */
static int cmp_tags(const void *a, const void *b)
{
    const char *const *pa = a;
    const char *const *pb = b;

    return strcmp(*pa, *pb);
}

char *tg_tagset_render(const TgTagSet *ts, size_t *out_len)
{
    qsort(ts->items, ts->count, sizeof *ts->items, cmp_tags);

    size_t total = 0;

    for (size_t i = 0; i < ts->count; ++i) {
        total += strlen(ts->items[i]);
    }

    if (ts->count > 0) {
        total += ts->count - 1;
    }

    char *out = malloc(total + 1);

    if (out == NULL) {
        fprintf(stderr, "tg: out of memory\n");
        return NULL;
    }

    char *w = out;

    for (size_t i = 0; i < ts->count; ++i) {
        if (i > 0) {
            *w++ = ',';
        }

        size_t n = strlen(ts->items[i]);
        memcpy(w, ts->items[i], n);
        w += n;
    }

    *w = '\0';
    *out_len = total;

    return out;
}
