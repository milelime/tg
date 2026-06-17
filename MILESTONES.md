# tg — implementation milestones

Implementation guide for building `tg` in C. Each milestone is independently
testable and builds on the previous. Code snippets are **illustrative pointers**,
not copy-paste solutions — you write the implementations.

Target: C11, Linux, built with `nob` (see `README.md`). The interesting systems
work is the xattr syscalls and the directory walk; everything else is plumbing.

Conventions used throughout:

- Extended-attribute calls come from `<sys/xattr.h>`: `lsetxattr`, `lgetxattr`,
  `llistxattr`, `lremovexattr`. Use the `l*` (no-follow-symlink) variants so you
  tag the **symlink itself**, not its target — the least-surprising default for a
  file tagger.
- On error these return `-1` and set `errno`. Translate `errno` once, in the
  xattr wrapper layer, into your own result enum so the rest of the code never
  touches `errno`.
- Exit codes: `0` ok, `1` usage error, `2` runtime (I/O / xattr) error.

Suggested file layout:

```
src/
  main.c       # CLI: arg parsing + command dispatch (done in M0)
  xattr.c/.h   # thin wrappers over the l*xattr syscalls
  tagset.c/.h  # tag parse/serialize/set-ops + read-modify-write helpers
  cmd.c/.h     # one function per subcommand (add/rm/tags/clear/find)
```

Add each new `.c` to the `sources[]` array in `nob.c` so it gets compiled.

---

## M0 — Dispatch skeleton (done)

`src/main.c` parses argv and routes the subcommand string to a `Command` enum
via `parse_command`, with usage/unknown-command handling and the exit-code
convention above. The command handlers are placeholder stubs that echo their
arguments.

**Acceptance:** `tg` with no args → usage, exit 1; `tg bogus` → error, exit 1;
`tg add foo bar` → reaches the add path.

---

## M1 — `xattr.c` / `xattr.h`: syscall wrappers (done)

**Goal:** safe get/set/list/remove over the `l*xattr` syscalls, with `errno`
mapped to a small result type.

Define your attribute name and a result enum in `xattr.h`:

```c
#define TG_XATTR_NAME "user.tags"

typedef enum {
    XATTR_OK,
    XATTR_ABSENT,       // attribute not set on this file
    XATTR_UNSUPPORTED,  // fs / namespace has no xattr support
    XATTR_NOFILE,       // the file itself does not exist
    XATTR_ACCESS,       // permission denied
    XATTR_NOSPACE,      // out of space / quota / value too large
    XATTR_TOOBIG,       // value did not fit the caller's buffer
    XATTR_ERROR,        // anything else
} XattrResult;
```

Suggested functions:

```c
// Reads user.tags into buf (capacity cap). On XATTR_OK, *out_len is the length.
XattrResult xattr_get(const char *path, char *buf, size_t cap, size_t *out_len);
XattrResult xattr_set(const char *path, const char *value, size_t len);
XattrResult xattr_remove(const char *path);  // ABSENT counts as success (idempotent)
```

Implementation notes:

- **The calls** (from `<sys/xattr.h>`, link nothing extra; glibc provides them):

  ```c
  ssize_t n = lgetxattr(path, TG_XATTR_NAME, buf, cap);
  if (n < 0) { /* map errno */ }
  *out_len = (size_t)n;
  ```

  ```c
  int rc = lsetxattr(path, TG_XATTR_NAME, value, len, 0 /* create or replace */);
  ```

  The final `lsetxattr` flag is `0` for create-or-replace, `XATTR_CREATE` to fail
  if it exists, `XATTR_REPLACE` to fail if absent. You want `0`.

- **errno mapping** (do it in one helper):
  - `ENODATA` → `XATTR_ABSENT` (get) / success (remove)
  - `ENOTSUP` (a.k.a. `EOPNOTSUPP`) → `XATTR_UNSUPPORTED`
  - `ERANGE` → `XATTR_TOOBIG` (buffer too small — see sizing)
  - `ENOENT` → `XATTR_NOFILE`
  - `EACCES` / `EPERM` → `XATTR_ACCESS`
  - `ENOSPC` / `EDQUOT` / `E2BIG` → `XATTR_NOSPACE`
  - else → `XATTR_ERROR`

- **Value buffer sizing.** Real tag sets are tiny. Use a fixed caller buffer
  (e.g. `char buf[4096]`) and treat `ERANGE` as `XATTR_TOOBIG` rather than
  implementing the two-call grow loop. (If you ever want unbounded: call with
  `size == 0` to learn the length, `malloc`, call again.)

- xattr values are **not** NUL-terminated on disk. Track the length explicitly;
  NUL-terminate yourself only when handing the bytes to string functions.

**Acceptance:** a throwaway `main` (or a test in M7) sets `user.tags` on a temp
file and reads it back; `getfattr -n user.tags <file>` shows the same bytes.

---

## M2 — `tagset.c` / `tagset.h`: the tag model (done)

**Goal:** parse/serialize the comma-separated value and do set algebra, plus
read-modify-write helpers on top of M1.

Storage contract (your on-disk format — keep it exact):

- value = tags joined by `,`, **sorted ascending** (`strcmp` order),
  **de-duplicated**.
- a tag token: non-empty, contains no comma, trimmed of surrounding ASCII
  whitespace. Reject (usage error) tokens that are empty after trimming or that
  contain a comma.
- decide and document case handling — recommend **case-sensitive, no
  normalization** (`ML` != `ml`); if you'd rather lowercase, do it in one place
  here.
- empty set ⇒ the caller should `xattr_remove`, never write `""`.

A simple representation: a growable array of owned C strings.

```c
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} TagSet;

void tagset_init(TagSet *ts);
void tagset_free(TagSet *ts);

// Parse a stored value (length-delimited, not NUL-terminated) into a sorted/deduped set.
bool tagset_parse(TagSet *ts, const char *value, size_t len);

// Add/remove a single tag (already validated). add returns true if newly added.
bool tagset_add(TagSet *ts, const char *tag);
bool tagset_remove(TagSet *ts, const char *tag);
bool tagset_has(const TagSet *ts, const char *tag);

// Render to a freshly malloc'd, NUL-terminated "a,b,c". Caller frees. *out_len excludes NUL.
char *tagset_render(const TagSet *ts, size_t *out_len);
```

Notes:

- Split on `,` with `memchr`; trim with pointer arithmetic over the token.
- Keep the array sorted on insert (binary search + shift), or sort with `qsort`
  and a `strcmp` comparator after bulk parsing, then drop adjacent duplicates.
- Validate the user-supplied `<tags>` argument before any filesystem work, so a
  bad tag fails fast with exit code 1.

A read-modify-write helper ties M1 + M2 together (put it in `cmd.c` or here):

```c
// Read user.tags, apply mutate(), write back (or remove if the set is empty).
typedef bool (*TagMutator)(TagSet *ts, void *ctx);
XattrResult tagset_update(const char *path, TagMutator mutate, void *ctx);
```

**Acceptance:** unit checks — `parse("b,a,a")` → `["a","b"]`; `render(parse(x))`
is stable; adding an existing tag returns false.

---

## M3 — mutating commands: `add`, `rm`, `tags`, `clear` (done)

**Goal:** the explicit-file commands work end to end. All operate on file paths
passed directly on the command line: `tg <cmd> <tags> <file>...` (for `tags` and
`clear` there is no `<tags>` argument).

- `add` — parse + validate `<tags>`, then for each file: read set, union the new
  tags, write back. Quiet on success (add a `-v` later if you want feedback).
- `rm` — same, but set difference; if the result is empty, remove the attribute.
- `tags` — for each file read and print `path\ttag1, tag2` (or just the tags for
  a single file). Absent attribute ⇒ print an empty set.
- `clear` — `xattr_remove` on each file.

Cross-cutting:

- **Per-file error isolation:** a missing/unsupported file must not abort the
  batch. Loop, handle each file's `XattrResult`, print `tg: <path>: <reason>` to
  `stderr`, remember that *something* failed, and continue. Exit `2` at the end if
  any file failed.
- Normal output to `stdout`, diagnostics to `stderr`.

**Acceptance:**
```sh
tg add a,b f1 f2 && tg tags f1      # -> a, b
tg rm a f1 && tg tags f1            # -> b
tg clear f1 && tg tags f1           # -> (empty); getfattr shows no user.tags
tg add x /nonexistent ; echo $?     # -> diagnostic on stderr, exit 2
```

---

## M4 — `find`: query a tree (done)

**Goal:** `tg find <tags> [path]` lists every regular file under `path` (default
`.`) whose tag set contains **all** query tags (AND semantics).

Walk the tree with `nftw` (`<ftw.h>`, define `_XOPEN_SOURCE 700`) — it handles
recursion for you:

```c
#define _XOPEN_SOURCE 700
#include <ftw.h>

static int visit(const char *path, const struct stat *sb, int typeflag, struct FTW *ftw) {
    if (typeflag != FTW_F) return 0;            // regular files only
    // xattr_get(path, ...) -> parse -> require every query tag present -> print path
    return 0;
}
// nftw(root, visit, 16 /* max fds */, FTW_PHYS /* don't follow symlinks */);
```

The query `TagSet` is global/static to the walk (nftw's callback has no user-data
parameter), parsed once before `nftw`. Matching: for each query tag, require
`tagset_has(file_set, q)`; print the path on a full match.

Alternative if you want to avoid the global: implement the recursion yourself
with `opendir`/`readdir`/`lstat`, threading a context struct through. `nftw` is
less code to start; switch later if you need per-walk state or parallelism.

Performance notes:

- Most files have no `user.tags`; `lgetxattr` returning `ENODATA` is one cheap
  syscall — fine to call per regular file.
- `FTW_PHYS` makes `nftw` not follow symlinks, matching your `l*xattr` choice.
- Per-file errors (permission denied) → skip, optionally warn under `-v`; never
  abort the whole walk. Return `0` from the callback to keep going.

**Acceptance:**
```sh
mkdir -p t/a/b && touch t/a/x t/a/b/y
tg add ml t/a/x ; tg add ml,paper t/a/b/y
tg find ml t                 # -> both paths
tg find ml,paper t           # -> only t/a/b/y
tg find ml /fs-without-xattr # -> clean "unsupported" message, not a crash
```

---

## M5 — polish (pick what you'll actually use) (done)

Small, high-value additions once the core works:

- `--any` / `--all` on `find` (default `--all`); `--any` = OR match.
- `-0` null-delimited output on `find` for piping into `xargs -0`.
- `tg find` with no path **and** no tags → list everything tagged under `.`.
- Color/`isatty(STDOUT_FILENO)` handling: only colorize when stdout is a TTY.
- `tg cp <src> <dst>` to copy a tag set between two existing files (handy when a
  tool dropped xattrs).
- Per-subcommand `--help`.

Keep flag parsing hand-rolled (`strcmp` / `strncmp`); `getopt` is fine too but
overkill for this surface.

---

## M6 — optional index for whole-disk `find` (stretch)

The live walk is fine for `~/dev`. For "query my entire home dir instantly", add
an **opt-in** index. This is the one genuinely systems-y extension.

Design sketch (don't build until you want it):

- Index file at `${XDG_DATA_HOME:-~/.local/share}/tg/index` — a map of
  `tag -> set of (device, inode, path)`. Keying on `(st_dev, st_ino)` (from
  `lstat`) means a moved file is the *same* entry; the path is a cached hint you
  re-validate on read.
- `tg index <root>` does a full walk and rebuilds. `tg find` uses the index when
  present and fresh, falling back to a live walk otherwise.
- **inotify** (`<sys/inotify.h>`: `inotify_init1`, `inotify_add_watch`) keeps it
  warm: a `tg watch <root>` daemon adds watches recursively and, on `IN_ATTRIB`
  (xattr changes fire this), `IN_MOVED_*`, `IN_CREATE`, `IN_DELETE`, updates the
  index incrementally. Handle `IN_Q_OVERFLOW` by scheduling a rescan. Recursive
  watching is manual (one watch per directory; add/remove as dirs appear/vanish)
  and is the bulk of the work.
- Always treat the index as a cache: on `find`, re-read `user.tags` from the path
  before printing, so a stale index can never report a wrong tag. The index
  narrows candidates; the filesystem stays the source of truth.

Only worth it if live `find` actually feels slow for you. Measure first.

---

## M7 — tests & build polish

- Add a `./nob test` path: compile a `tests/` driver (or a `--test` flag in a
  separate target) and run it. Keep `tagset` tests filesystem-free.
- For xattr/`find`, create files under a temp dir (`mkdtemp`), tag them, assert,
  clean up — and **skip cleanly** if the temp filesystem returns
  `XATTR_UNSUPPORTED` (CI tmpfs often lacks `user.` xattrs).
- Build flags: keep `-Wall -Wextra -Wpedantic`. Add a release variant in `nob.c`
  (e.g. `-O2 -DNDEBUG`) selected by `./nob release`; keep `-g` for the default
  debug build. Consider `-fsanitize=address,undefined` for a `./nob debug` build
  while developing.
