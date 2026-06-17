/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
/* main.c - argument parsing and subcommand dispatch. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"

typedef enum {
    CMD_ADD,
    CMD_RM,
    CMD_TAGS,
    CMD_CLEAR,
    CMD_FIND,
    CMD_CP,
    CMD_UNKNOWN,
} Command;

static Command parse_command(const char *s)
{
    if (strcmp(s, "add") == 0) return CMD_ADD;
    if (strcmp(s, "rm") == 0) return CMD_RM;
    if (strcmp(s, "tags") == 0) return CMD_TAGS;
    if (strcmp(s, "clear") == 0) return CMD_CLEAR;
    if (strcmp(s, "find") == 0) return CMD_FIND;
    if (strcmp(s, "cp") == 0) return CMD_CP;
    return CMD_UNKNOWN;
}

static void usage(FILE *out)
{
    fputs("tg - tag files with extended attributes that travel with the file\n"
          "\n"
          "usage: tg <command> [args]\n"
          "\n"
          "  add    <tags> <file>...       add tags to files\n"
          "  rm     <tags> <file>...       remove tags from files\n"
          "  tags   <file>...              list tags on files\n"
          "  clear  <file>...              remove all tags from files\n"
          "  find   [opts] [tags] [path]   list files under path matching tags\n"
          "  cp     <src> <dst>            copy tags from one file to another\n"
          "\n"
          "<tags> is a comma-separated list, e.g. work,urgent.\n"
          "See 'tg <command> --help' for per-command options.\n",
          out);
}

static void help_for(Command cmd, FILE *out)
{
    switch (cmd) {
    case CMD_ADD:
        fputs("usage: tg add <tags> <file>...\n\n"
              "Add each tag in <tags> to every <file>. Tags already present are\n"
              "left as-is.\n",
              out);
        break;
    case CMD_RM:
        fputs("usage: tg rm <tags> <file>...\n\n"
              "Remove each tag in <tags> from every <file>. Removing the last tag\n"
              "deletes the attribute. Absent tags are ignored.\n",
              out);
        break;
    case CMD_TAGS:
        fputs("usage: tg tags <file>...\n\n"
              "Print each file's tags as 'path<TAB>a,b,c'. Untagged files print an\n"
              "empty list.\n",
              out);
        break;
    case CMD_CLEAR:
        fputs("usage: tg clear <file>...\n\n"
              "Remove all tags from every <file>.\n",
              out);
        break;
    case CMD_FIND:
        fputs("usage: tg find [--any|--all] [-0] [tags] [path]\n\n"
              "List regular files under <path> (default: .) whose tags match\n"
              "<tags>, one path per line. With no <tags>, list every tagged file.\n"
              "\n"
              "  --all   match files having every tag in <tags> (default)\n"
              "  --any   match files having any tag in <tags>\n"
              "  -0      separate paths with NUL instead of newline, for xargs -0\n",
              out);
        break;
    case CMD_CP:
        fputs("usage: tg cp <src> <dst>\n\n"
              "Copy the tag set from <src> onto <dst>, replacing dst's tags. If\n"
              "<src> has no tags, dst's tags are removed.\n",
              out);
        break;
    case CMD_UNKNOWN:
        usage(out);
        break;
    }
}

static bool wants_help(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "help") == 0) {
        usage(stdout);
        return 0;
    }

    Command cmd = parse_command(argv[1]);
    if (cmd == CMD_UNKNOWN) {
        fprintf(stderr, "tg: unknown command '%s'\n", argv[1]);
        usage(stderr);
        return 1;
    }

    if (wants_help(argc, argv)) {
        help_for(cmd, stdout);
        return 0;
    }

    switch (cmd) {
    case CMD_ADD:
        if (argc < 4) {
            fprintf(stderr, "usage: tg add <tags> <file>...\n");
            return 1;
        }
        return cmd_add(argv[2], argc - 3, &argv[3]);
    case CMD_RM:
        if (argc < 4) {
            fprintf(stderr, "usage: tg rm <tags> <file>...\n");
            return 1;
        }
        return cmd_rm(argv[2], argc - 3, &argv[3]);
    case CMD_TAGS:
        if (argc < 3) {
            fprintf(stderr, "usage: tg tags <file>...\n");
            return 1;
        }
        return cmd_tags(argc - 2, &argv[2]);
    case CMD_CLEAR:
        if (argc < 3) {
            fprintf(stderr, "usage: tg clear <file>...\n");
            return 1;
        }
        return cmd_clear(argc - 2, &argv[2]);
    case CMD_FIND:
        return cmd_find(argc - 2, &argv[2]);
    case CMD_CP:
        if (argc != 4) {
            fprintf(stderr, "usage: tg cp <src> <dst>\n");
            return 1;
        }
        return cmd_cp(argv[2], argv[3]);
    case CMD_UNKNOWN:
        break; /* unreachable: handled above */
    }

    return 0;
}
