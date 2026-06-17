/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/*
 * cmd.h - subcommand implementations.
 *
 * Each returns a process exit code: 0 ok, 1 usage error, 2 runtime error.
 * The batch commands process every file, reporting per-file errors to stderr
 * and continuing; a 2 means at least one file failed. `spec` is the
 * comma-separated tag argument, e.g. "ml,paper".
 */
#ifndef TG_CMD_H
#define TG_CMD_H

int cmd_add(const char *spec, int nfiles, char **files);
int cmd_rm(const char *spec, int nfiles, char **files);
int cmd_tags(int nfiles, char **files);
int cmd_clear(int nfiles, char **files);
int cmd_cp(const char *src, const char *dst);

/* argc/argv are the arguments following the "find" subcommand. */
int cmd_find(int argc, char **argv);

#endif /* TG_CMD_H */
