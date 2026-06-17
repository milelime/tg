/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/*
 * xattr.h - read and write the user.tags extended attribute.
 *
 * All operations act on the symlink itself, never its target. Values are raw
 * byte buffers, not NUL-terminated strings; track lengths explicitly. See
 * tagset.h for the format stored inside the attribute.
 */
#ifndef TG_XATTR_H
#define TG_XATTR_H

#include <stddef.h>

#define TG_XATTR_NAME "user.tags"

typedef enum {
    TG_XATTR_OK,
    TG_XATTR_ABSENT,      /* attribute is not set on the file */
    TG_XATTR_UNSUPPORTED, /* filesystem lacks user xattr support */
    TG_XATTR_NOFILE,      /* path does not exist */
    TG_XATTR_ACCESS,      /* permission denied */
    TG_XATTR_NOSPACE,     /* out of space, over quota, or value too large */
    TG_XATTR_TOOBIG,      /* value did not fit the supplied buffer */
    TG_XATTR_ERROR,       /* other / unexpected errno */
} TgXattrResult;

/* Read into buf[0..cap); on TG_XATTR_OK, *out_len holds the byte count. */
TgXattrResult tg_xattr_get(const char *path, char *buf, size_t cap, size_t *out_len);

/* Replace the attribute with value[0..len). */
TgXattrResult tg_xattr_set(const char *path, const char *value, size_t len);

/* Delete the attribute. A missing attribute is treated as success. */
TgXattrResult tg_xattr_remove(const char *path);

#endif /* TG_XATTR_H */
