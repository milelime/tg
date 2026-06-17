/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/* xattr.c - thin l*xattr wrappers; errno is translated only here. */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include "xattr.h"

static TgXattrResult map_errno(void)
{
    switch (errno) {
    case ENODATA:
        return TG_XATTR_ABSENT;
    case ENOTSUP:
        return TG_XATTR_UNSUPPORTED;
    case ERANGE:
        return TG_XATTR_TOOBIG;
    case ENOENT:
        return TG_XATTR_NOFILE;
    case EACCES:
    case EPERM:
        return TG_XATTR_ACCESS;
    case ENOSPC:
    case EDQUOT:
    case E2BIG:
        return TG_XATTR_NOSPACE;
    default:
        return TG_XATTR_ERROR;
    }
}

TgXattrResult tg_xattr_get(const char *path, char *buf, size_t cap, size_t *out_len)
{
    ssize_t n = lgetxattr(path, TG_XATTR_NAME, buf, cap);
    if (n < 0) {
        return map_errno();
    }
    *out_len = (size_t)n;
    return TG_XATTR_OK;
}

TgXattrResult tg_xattr_set(const char *path, const char *value, size_t len)
{
    if (lsetxattr(path, TG_XATTR_NAME, value, len, 0) < 0) {
        return map_errno();
    }
    return TG_XATTR_OK;
}

TgXattrResult tg_xattr_remove(const char *path)
{
    if (lremovexattr(path, TG_XATTR_NAME) < 0) {
        return (errno == ENODATA) ? TG_XATTR_OK : map_errno();
    }
    return TG_XATTR_OK;
}
