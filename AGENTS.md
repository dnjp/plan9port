# Agent Reference: plan9port (dnjp fork)

This document is intended for AI agents working in this repository. It captures
architectural decisions, non-obvious conventions, and hard-won debugging knowledge
from the development history of this fork.

**Agents: keep this document current.** After completing any non-trivial task,
add new entries to the debugging table, document new architectural decisions, and
update any sections whose behaviour has changed. Also update
`.cursor/rules/plan9port.mdc` at the dotfiles root if the change is broadly
relevant across sessions.

## Repository overview

This is a fork of [9fans/plan9port](https://github.com/9fans/plan9port) with
macOS-specific enhancements. The default branch is `dnjp`. Upstream is tracked
as the `upstream` remote.

Notable additions over upstream:
- **Ruler integration in acme**: per-window font/tabstop/comment directives via the `ruler` daemon.
- **Font rendering speed**: pre-loading of the fixed font at startup, `rfget` HiDPI cache fix, 64-entry LRU subfont cache (`src/cmd/acme/cache.c`).
- **Shift+click selection** in acme (`textselectextend` in `src/cmd/acme/text.c`).
- **`editinacme`**: reliable `$EDITOR` wrapper for opening files in acme.
- **macOS app bundles**: Acme, 9term, Sam show their own Dock icon/name via bundle-internal `devdraw`.
- **`plumb-edit`**: replaces `plumb client $editor` with a `plumb start` script.

## Build system

plan9port uses `mk` (not `make`). Key commands:

```sh
mk all          # build everything in current directory
mk install      # build and install to $PLAN9/bin
mk o.acme.bin   # build local test binary (acme)
```

`src/mkmany` defines generic rules for multi-target directories.
`src/mkone` defines rules for single-target directories.
Both automatically `codesign --force --sign -` binaries on macOS after install.

**`9term/mkfile` note**: `9term.bin` is excluded from `TARG` and has its own
explicit `all`/`install` rules because `mkmany`'s generic rule would look for a
nonexistent `9term.bin.c`. The explicit rule links `9term.o` + extra objects.

## Acme internals

### Font pipeline

```
acme startup
  → rfget(1, TRUE, FALSE, nil)        # pre-load fixed font into reffonts[1]
  → stringwidth(rf1->f, "abc...")     # pre-warm Basic Latin subfont cache

file open (openfile / plumbshow in look.c)
  → rulerprefont(w)                   # query ruler, set font BEFORE textload
  → textload(...)                     # fill frame (uses correct font, no re-render)
  → rulerapply(w)                     # apply remaining directives (tabstop, etc.)
```

`rfget` compares font names against both `f->name` (LoDPI part) and `f->namespec`
(full comma-separated HiDPI string) to correctly hit the cache for HiDPI fonts.

### Subfont cache (`src/cmd/acme/cache.c`)

Overrides libdraw's single-entry `lookupsubfont`/`installsubfont`/`uninstallsubfont`
with a 64-entry LRU cache. Linked before `libdraw.a` in `src/cmd/acme/mkfile`.
Prevents constant cache thrashing when variable and fixed fonts are both in use
(each requires different subfonts even for Basic Latin characters).

### Shift+click selection

`textselectextend(Text *t)` in `text.c`. Called from `acme.c` when button-1 is
pressed with the Shift modifier. Handles both the "no existing selection" case
(cursor is the anchor) and the "existing selection" case (moves the nearer end).
`t->cursoratq1` tracks which end is the cursor across successive shift+clicks.

### Ruler integration

`ruler.c` in `src/cmd/acme/`:
- `rulerprefont(w)`: queries ruler for font directive only, applies before frame fill.
- `rulerapply(w)`: full directive application (font, tabstop, comment format, etc.).
- `rulerlog(...)`: debug logging, enabled by setting `rulerdebug=1` in environment.
- `winresize(w, w->r, FALSE, TRUE)` is used instead of `colgrow` for font changes
  (redraws only the affected window, not the entire column).

## Plumbing

### `plumb/basic`

The `dnjp` fork uses `plumb start $plan9/bin/plumb-edit` for file-opening rules
instead of upstream's `plumb client $editor`. This means nothing ever opens the
`edit` port as a 9P client, so the port must be declared explicitly:

```
# declarations of ports without rules
# edit is declared here because the file-opening rulesets use "plumb start"
# rather than "plumb client", so no client ever registers the port dynamically.
plumb to edit
plumb to seemail
plumb to showmail
```

**Plumber parse rules** (from `src/cmd/plumb/rules.c`):
- A ruleset may have at most one `plumb to X` (sets the destination port).
- A ruleset may have at most one `plumb start` or `plumb client` action.
- Having both `plumb to X` and `plumb start Y` in the same ruleset is **valid** —
  this is the normal upstream pattern.
- Having two `plumb to` lines in one ruleset causes "too many ports" error.

### `bin/plumb-edit`

Called by the plumber when a file match fires. Starts acme if needed (waits for
`9p stat acme`), then sends `plumb -d edit $file` directly to acme's edit port.
Using `-d edit` bypasses rule re-evaluation and prevents recursive invocation.

### `bin/editinacme`

Used as `$EDITOR` (e.g. `GIT_EDITOR=editinacme`). Flow:

1. If acme is not running: start it, wait for `9p stat acme`.
2. Start `9p read acme/log > $logfifo &` **before** plumbing the file.
3. Send `plumb -d edit $file`.
4. Run `awk` reading from `$logfifo`; signal via `$donefifo` when `del` event seen.
5. Kill background processes with `kill $logpid $awkpid | rc`.

**Why start the log reader first**: the `del` event could fire before `awk` starts
if the user closes the window quickly. The fifo ensures no events are missed.

**Why `kill ... | rc`**: plan9port's `kill` command prints shell commands to stdout
but does not execute them. The output must be piped to `rc` or `sh`.

**Why `plumb/edit` existing ≠ acme running**: `plumb to edit` is a static
declaration in `plumb/basic`. The port always exists once the plumber starts.
Use `9p stat acme` to check if acme is actually running.

## macOS app bundles (`mac/`)

See `mac/MacOS.md` for full documentation.

**Key point**: `devdraw` is the `NSApplication` process. To give each app its own
Dock icon and menu bar name:
1. `mk apps` copies `$PLAN9/bin/devdraw` into each bundle's `Contents/MacOS/`.
2. Each launcher script sets `DEVDRAW=$(dirname "$0")/devdraw`.
3. `devdraw`'s `applicationDidFinishLaunching` reads `CFBundleName` and
   `CFBundleIconFile` from `[NSBundle mainBundle]`.

### Multi-window / Dock menu IPC

Each "New Window" spawns a separate process. A primary/secondary model in
`mac-screen.m` keeps all windows under one Dock icon:

- **Primary** (no `P9P_SECONDARY`): `NSApplicationActivationPolicyRegular`,
  runs a Unix domain socket server at `/tmp/p9p-winreg-<bundleID>`.
- **Secondary** (`P9P_SECONDARY=1`, set by `open -n --env`):
  `NSApplicationActivationPolicyAccessory`, connects to primary's socket and
  sends `title <pid> <label>\n` and `close <pid>\n` messages.
- **Promotion**: when the primary exits, the first secondary to detect the
  socket closure calls `winreg_promote()`, which starts a new server, switches
  to `Regular` policy, and activates the app after a 200 ms delay.
- **Dock menu**: primary builds it from local `[NSApp windows]` (using
  `winreg_pending_title` for the label) plus the `winreg[]` array of remote
  windows.
- **Title bar vs. Dock label**: `setlabel:` sets the `NSWindow` title to
  `CFBundleName` (stable, e.g. "acme") and stores the full path in
  `winreg_pending_title` for the Dock menu only.
- **libc.h conflicts**: `libc.h` redefines `accept`/`listen`/`close`/`write`.
  The IPC section `#undef`s them before use and `#define`s them back afterwards.

### Acme window labels

`acme.c` calls `drawsetlabel()` at startup (with `wdir`) and on focus change
(with `w->body.file->name`) so the Dock menu shows meaningful per-window paths.

## Shell environment

Files in `shell/` ending in `.sh.in`, `.fish.in`, `.rc.in` are templates.
`SETUP` processes them with `sed "s|/usr/local/plan9|$PLAN9|g" | sudo tee`
(sudo required because `/usr/local/plan9` is root-owned after `INSTALL`).

**Always edit the `.in` files.** The generated files (`plan9.sh`, `p9p-session.sh`,
etc.) are produced at install time and should not be committed.

## Common debugging

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `plumber: ruleset has more than one client or start action` | Two `plumb to` lines in one ruleset | Move extra `plumb to` to top-level declarations block |
| `plumb file does not exist` when acme tries to open edit port | Missing `plumb to edit` declaration | Add `plumb to edit` to top of `plumb/basic` |
| `9p: mount: dial ... Connection refused` after `9p stat acme` succeeds | Race: acme's 9P server not fully up | Wait in a loop on `9p stat acme` |
| acme reopens continuously after closing | `plumb-edit` calling `plumb $file` without `-d`, re-triggering rules | Use `plumb -d edit $file` |
| devdraw shows Glenda icon / "devdraw" in menu bar | devdraw not running from within an app bundle | Set `DEVDRAW` to bundle-internal copy; run `mk apps` |
| `9term.bin.o: no recipe` during build | `9term.bin` incorrectly listed in `TARG` in `9term/mkfile` | Keep `9term.bin` out of `TARG`; use explicit rules |
| `launchctl load` → `Load failed: 5: Input/output error` | `launchctl load/unload` deprecated since macOS 10.10 | Use `launchctl bootstrap gui/$(id -u)` / `bootout gui/$(id -u)` |
| devdraw "New Window" menu action silently does nothing | `NSMenuItem` action not dispatched — target defaults to first responder chain, which has no `newWindow:` handler | Set `[menuItem setTarget:self]` explicitly on the `AppDelegate` |
| devdraw crashes or corrupts state after `fork()` | `fork()` after `[NSApplication sharedApplication]` is unsafe on macOS | Use `posix_spawn` instead |
| New window process crashes: `threads in main proc exited w/o threadmaybackground` | Child spawned without a window server connection | Use `posix_spawn("/usr/bin/open", "-n", bundlePath)` — goes through Launch Services so the new devdraw gets a proper window server connection |
| New window: `initdraw: muxrpc: unexpected eof` | Child inherits `DEVDRAW` env var pointing to parent's devdraw socket | Unset `DEVDRAW` in child environment, or use `open -n` which launches fresh via Launch Services |
| Font flash on file open | `rulerapply` running after `textload` | Call `rulerprefont(w)` before `textload` |
| Slow font load (~200ms) | `rfget` cache miss on HiDPI font name | Compare against both `f->name` and `f->namespec` in `rfget` |
| App launched from Dock: `exec 9pserve: No such file or directory` | Dock-launched apps inherit a minimal `PATH` without `$PLAN9/bin` | Add `export PATH="$PLAN9/bin:$PATH"` early in the bundle launcher script |
| App launched from Dock: wrong fonts or no fonts | Dock launch doesn't source `~/.bash_profile`; `fixfont`/`varfont` never set | The launcher sets `PLAN9` and `PATH`; `bin/acme` wrapper sources `plan9.sh` if `fixfont` is unset |
| App launched from Dock: `SIGKILL` / "Code Signature Invalid" | Copying `devdraw` into the bundle invalidates its signature | Use `codesign --force --deep --sign - /Applications/App.app` — `--deep` is required to re-seal the whole bundle |
| Secondary instances lose Dock icon when primary exits | `NSApplicationActivationPolicyAccessory` persists after primary dies | `winreg_promote()` switches to `Regular` policy; needs 200 ms delay before `activateIgnoringOtherApps` for Dock to pick up the change |
| New secondary always thinks a primary exists (stale socket file) | Checking socket file existence on disk is unreliable — stale files persist after crashes | Detect secondary status via `P9P_SECONDARY` env var (set by `open -n --env`), not by probing the socket path |
| `9c error: too many arguments to function call` for `accept`/`listen`/`close`/`write` in `mac-screen.m` | `libc.h` redefines these to `p9accept` etc. with different signatures | `#undef` the macros before the IPC section, declare POSIX prototypes explicitly, `#define` them back afterwards |
| `cannot refer to declaration with an array type inside block` in Objective-C block | C-style stack arrays cannot be captured by blocks | Heap-allocate with `malloc`/`strdup`, `free` after use inside the block |
| Dock menu shows app name for all windows instead of file paths | `setlabel:` was setting `[win setTitle:label]`, making window title and Dock label the same | Set `[win setTitle:bundleName]` for the title bar; store full label separately in `winreg_pending_title` for the Dock menu |
