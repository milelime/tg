/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Alexey Ayzin
 */
#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "build"
#define BIN BUILD_DIR "/tg"
#define SOMAJOR "0"

static const char *cli_sources[] = {
    "src/main.c",
    "src/cmd.c",
    "src/xattr.c",
    "src/tagset.c",
};

/* Inputs that should trigger a rebuild of the CLI. */
static const char *cli_inputs[] = {
    "src/main.c", "src/cmd.c",     "src/xattr.c",  "src/tagset.c",
    "src/cmd.h",  "src/tagset.h",  "src/xattr.h",  "VERSION",
};

static const char *common_cflags[] = {
    "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-g", "-Isrc",
};

static const char *VER;

/* Read and trim the single-line VERSION file. */
static const char *read_version(void)
{
    static char buf[64] = "0.0.0";
    Nob_String_Builder sb = {0};
    if (nob_read_entire_file("VERSION", &sb)) {
        Nob_String_View sv = nob_sv_trim(nob_sb_to_sv(sb));
        size_t n = sv.count < sizeof(buf) - 1 ? sv.count : sizeof(buf) - 1;
        memcpy(buf, sv.data, n);
        buf[n] = '\0';
    }
    return buf;
}

static void add_common(Nob_Cmd *cmd)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(common_cflags); i++) {
        nob_cmd_append(cmd, common_cflags[i]);
    }
}

static bool build_cli(void)
{
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    add_common(&cmd);
    nob_cmd_append(&cmd, nob_temp_sprintf("-DTG_VERSION=\"%s\"", VER));
    nob_cc_output(&cmd, BIN);
    for (size_t i = 0; i < NOB_ARRAY_LEN(cli_sources); i++) {
        nob_cmd_append(&cmd, cli_sources[i]);
    }
    return nob_cmd_run(&cmd);
}

/* Build libtg.a and libtg.so.<VER> from the tagset + xattr objects. */
static bool build_libs(void)
{
    const char *srcs[] = {"src/tagset.c", "src/xattr.c"};
    const char *objs[] = {BUILD_DIR "/tagset.o", BUILD_DIR "/xattr.o"};

    Nob_Cmd cmd = {0};
    for (size_t i = 0; i < NOB_ARRAY_LEN(srcs); i++) {
        nob_cc(&cmd);
        add_common(&cmd);
        nob_cmd_append(&cmd, "-fPIC", "-c", srcs[i], "-o", objs[i]);
        if (!nob_cmd_run(&cmd)) return false;
    }

    nob_cmd_append(&cmd, "ar", "rcs", BUILD_DIR "/libtg.a", objs[0], objs[1]);
    if (!nob_cmd_run(&cmd)) return false;

    nob_cc(&cmd);
    nob_cmd_append(&cmd, "-shared", "-Wl,-soname,libtg.so." SOMAJOR, "-o",
                   nob_temp_sprintf(BUILD_DIR "/libtg.so.%s", VER), objs[0], objs[1]);
    return nob_cmd_run(&cmd);
}

/* Render tg.pc from tg.pc.in for the given prefix. */
static bool build_pc(const char *prefix)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "sed", "-e", nob_temp_sprintf("s:@PREFIX@:%s:", prefix),
                   "-e", nob_temp_sprintf("s:@VERSION@:%s:", VER), "tg.pc.in");
    return nob_cmd_run(&cmd, .stdout_path = BUILD_DIR "/tg.pc");
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return (v && *v) ? v : fallback;
}

/* Install/uninstall honor PREFIX and DESTDIR, matching the Makefile. */
static int do_install(void)
{
    const char *prefix = env_or("PREFIX", "/usr/local");
    const char *destdir = env_or("DESTDIR", "");
    const char *bindir = nob_temp_sprintf("%s%s/bin", destdir, prefix);
    const char *libdir = nob_temp_sprintf("%s%s/lib", destdir, prefix);
    const char *incdir = nob_temp_sprintf("%s%s/include/tg", destdir, prefix);
    const char *pcdir = nob_temp_sprintf("%s%s/lib/pkgconfig", destdir, prefix);
    const char *man = nob_temp_sprintf("%s%s/share/man", destdir, prefix);

    if (!build_cli() || !build_libs() || !build_pc(prefix)) return 1;

    Nob_Cmd cmd = {0};
#define INST(...)                                          \
    do {                                                   \
        nob_cmd_append(&cmd, __VA_ARGS__);                 \
        if (!nob_cmd_run(&cmd)) return 1;                  \
    } while (0)

    INST("install", "-d", bindir, libdir, incdir, pcdir,
         nob_temp_sprintf("%s/man1", man), nob_temp_sprintf("%s/man3", man),
         nob_temp_sprintf("%s/man5", man));

    INST("install", "-m", "755", BIN, nob_temp_sprintf("%s/tg", bindir));
    INST("install", "-m", "644", BUILD_DIR "/libtg.a",
         nob_temp_sprintf("%s/libtg.a", libdir));
    INST("install", "-m", "755", nob_temp_sprintf(BUILD_DIR "/libtg.so.%s", VER),
         nob_temp_sprintf("%s/libtg.so.%s", libdir, VER));
    INST("ln", "-sf", nob_temp_sprintf("libtg.so.%s", VER),
         nob_temp_sprintf("%s/libtg.so." SOMAJOR, libdir));
    INST("ln", "-sf", "libtg.so." SOMAJOR, nob_temp_sprintf("%s/libtg.so", libdir));
    INST("install", "-m", "644", "src/tagset.h", "src/xattr.h", incdir);
    INST("install", "-m", "644", BUILD_DIR "/tg.pc",
         nob_temp_sprintf("%s/tg.pc", pcdir));
    INST("install", "-m", "644", "man/tg.1", nob_temp_sprintf("%s/man1/tg.1", man));
    INST("install", "-m", "644", "man/libtg.3", nob_temp_sprintf("%s/man3/libtg.3", man));
    INST("install", "-m", "644", "man/tg.5", nob_temp_sprintf("%s/man5/tg.5", man));
#undef INST

    nob_log(NOB_INFO, "installed to %s%s", destdir, prefix);
    return 0;
}

static int do_uninstall(void)
{
    const char *prefix = env_or("PREFIX", "/usr/local");
    const char *destdir = env_or("DESTDIR", "");
    const char *p = nob_temp_sprintf("%s%s", destdir, prefix);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "rm", "-f",
                   nob_temp_sprintf("%s/bin/tg", p),
                   nob_temp_sprintf("%s/lib/libtg.a", p),
                   nob_temp_sprintf("%s/lib/libtg.so.%s", p, VER),
                   nob_temp_sprintf("%s/lib/libtg.so." SOMAJOR, p),
                   nob_temp_sprintf("%s/lib/libtg.so", p),
                   nob_temp_sprintf("%s/lib/pkgconfig/tg.pc", p),
                   nob_temp_sprintf("%s/share/man/man1/tg.1", p),
                   nob_temp_sprintf("%s/share/man/man3/libtg.3", p),
                   nob_temp_sprintf("%s/share/man/man5/tg.5", p));
    if (!nob_cmd_run(&cmd)) return 1;
    nob_cmd_append(&cmd, "rm", "-rf", nob_temp_sprintf("%s/include/tg", p));
    return nob_cmd_run(&cmd) ? 0 : 1;
}

static int do_dist(void)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "git", "archive", "--format=tar.gz",
                   nob_temp_sprintf("--prefix=tg-%s/", VER), "-o",
                   nob_temp_sprintf("tg-%s.tar.gz", VER), "HEAD");
    return nob_cmd_run(&cmd) ? 0 : 1;
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    VER = read_version();

    if (!nob_mkdir_if_not_exists(BUILD_DIR)) return 1;

    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "install") == 0) return do_install();
    if (strcmp(sub, "uninstall") == 0) return do_uninstall();
    if (strcmp(sub, "dist") == 0) return do_dist();
    if (strcmp(sub, "lib") == 0) return build_libs() ? 0 : 1;

    /* Default: build the CLI if any input changed. */
    int rebuild = nob_needs_rebuild(BIN, cli_inputs, NOB_ARRAY_LEN(cli_inputs));
    if (rebuild < 0) return 1;
    if (rebuild) {
        if (!build_cli()) return 1;
    } else {
        nob_log(NOB_INFO, "%s is up to date", BIN);
    }

    /* `./nob run -- <args>` builds (above) then runs the binary. */
    if (strcmp(sub, "run") == 0) {
        Nob_Cmd cmd = {0};
        int start = 2;
        if (start < argc && strcmp(argv[start], "--") == 0) start++;
        nob_cmd_append(&cmd, BIN);
        for (int i = start; i < argc; i++) nob_cmd_append(&cmd, argv[i]);
        if (!nob_cmd_run(&cmd)) return 1;
    }

    return 0;
}
