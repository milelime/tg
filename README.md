# tg

Tag files with extended attributes that travel with the file.

```sh
tg add paper,ml ~/docs/attention.pdf   # tag a file
tg tags ~/docs/attention.pdf           # -> ml,paper
tg find ml ~/docs                      # list files under ~/docs tagged ml
```

Tags live in the file's own metadata (a POSIX extended attribute), not in a
sidecar database keyed by path. They survive moves and renames, and ride along
on copies made with xattr-aware tools (`mv`, `cp --preserve=xattr`, `rsync -X`,
`tar --xattrs`). No daemon, no database, no config: the file is the source of
truth.

## How it works

Each tagged file carries one extended attribute, `user.tags`, holding a
comma-separated, sorted, de-duplicated list:

```
user.tags = "ml,paper"
```

`add` and `rm` read the value, modify the set, and write it back; removing the
last tag deletes the attribute. `find` walks a directory tree and reads
`user.tags` from each regular file. Tags are case-sensitive and may not contain
a comma. There is no lock-in; standard tools work too:

```sh
getfattr -n user.tags file.pdf
setfattr -x user.tags file.pdf   # remove tags by hand
```

## Commands

```
tg add   <tags> <file>...           add tags to files
tg rm    <tags> <file>...           remove tags from files
tg tags  <file>...                  list tags on files, as 'path<TAB>a,b,c'
tg clear <file>...                  remove all tags from files
tg find  [--any|--all] [-0] [tags] [path]
                                    list files under path (default .) matching tags
tg cp    <src> <dst>                copy the tag set from src to dst
```

`<tags>` is a single comma-separated argument, e.g. `paper,ml,to-read`.

`find` matches files having every tag (`--all`, the default) or any tag
(`--any`); `-0` separates results with NUL for `xargs -0`; with no `<tags>` it
lists every tagged file. Run `tg <command> --help` for details.

Exit status: `0` success, `1` usage error, `2` runtime error.

## Building

Two equivalent build paths. For development, [`nob`](https://github.com/tsoding/nob.h),
a single-header C build tool; bootstrap it once, afterwards it rebuilds itself:

```sh
cc -o nob nob.c          # one-time bootstrap
./nob                    # build -> build/tg
./nob run -- find ml .   # build, then run with arguments
```

For installing and packaging, a standard Makefile:

```sh
make                                 # build tg, libtg.a, libtg.so
sudo make install                    # to /usr/local by default
make install PREFIX=/usr DESTDIR=pkg # staged install, for packagers
make dist                            # tg-<version>.tar.gz release tarball
```

`./nob install` and `./nob dist` do the same as their `make` counterparts.
Installation lays out the binary, the `libtg` static and shared libraries, the
`tg/` headers, a `pkg-config` file, and the `tg(1)`, `tg(5)`, and `libtg(3)`
manual pages.

Requires a C11 compiler and Linux. Editor configs for `clangd` (`.clangd`) and
`clang-format` (`.clang-format`) are included.

## Library

The engine is also a library, `libtg`, split into a tag-set layer and an xattr
layer. Link against it with `pkg-config`:

```c
#include <tg/tagset.h>
#include <tg/xattr.h>
```

```sh
cc example.c $(pkg-config --cflags --libs tg)
```

See `libtg(3)` for the API.

## Requirements and limitations

- Needs a filesystem with `user.` xattr support: ext4, btrfs, xfs, zfs, f2fs.
  Most FAT/exFAT volumes, some network mounts, and bare `tmpfs` lack it.
- xattrs are not preserved by every copy tool. `mv` keeps them within a
  filesystem; `cp` needs `--preserve=xattr`, `rsync` needs `-X`, archives need
  `--xattrs`. This is a property of xattrs, not of `tg`.
- `find` is a live tree walk, fast for home-directory-sized trees. An optional
  indexed mode for whole-disk queries is planned (see `MILESTONES.md`).

## Status

Early. Please use and contribute if it interests you.
