/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/*
 * tagset.h - an in-memory set of tags.
 *
 * Serialized form (what lives in user.tags) is a comma-separated, sorted,
 * de-duplicated list, e.g. "ml,paper". Tags are case-sensitive, may not
 * contain a comma, and are trimmed of surrounding whitespace. The empty set
 * has no serialized form; callers remove the attribute instead of writing "".
 */
#ifndef TG_TAGSET_H
#define TG_TAGSET_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **items; /* owned strings, sorted by tg_tagset_render */
    size_t count;
    size_t cap;
} TgTagSet;

void tg_tagset_init(TgTagSet *ts);
void tg_tagset_free(TgTagSet *ts);

/* Parse value[0..len) (not NUL-terminated) into ts, merging into any contents. */
bool tg_tagset_parse(TgTagSet *ts, const char *value, size_t len);

bool tg_tagset_add(TgTagSet *ts, const char *tag);    /* false if already present */
bool tg_tagset_remove(TgTagSet *ts, const char *tag); /* false if not present */
bool tg_tagset_has(const TgTagSet *ts, const char *tag);

/* Render to a malloc'd, NUL-terminated "a,b,c"; *out_len excludes the NUL.
 * Caller frees. Returns NULL on allocation failure. */
char *tg_tagset_render(const TgTagSet *ts, size_t *out_len);

#endif /* TG_TAGSET_H */
