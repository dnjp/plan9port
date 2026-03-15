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
- **`plumb-edit`**: script that opens files in acme; used as `$editor` in plumbing rules via `plumb client $editor`.
- **`Plumb.app` file routing**: double-clicking any file in Finder routes it through the plumber, dispatching to acme, Preview, QuickTime, etc. based on type.
- **`p9p-open`**: thin wrapper around `open -b <bundleID>` used by `plumb/macos`; reassembles space-split filenames from `buildargv`.
- **9term enhancements** (see `src/cmd/9term/`):
  - `-F varfont`: proportional font for button-2 popup menus; fixed font (`-f`) for terminal text.
  - **Right-click plumb**: button 3 plumbs the word under the pointer (or the current selection).
  - **Ctrl+C / Ctrl+D**: send interrupt / EOT unconditionally, even in raw mode.
  - **Ctrl+K**: delete from cursor to end of line (discards text).
  - **Word movement**: Alt+Left/Right moves by word; Cmd+Left/Right moves to start/end of line (respecting the prompt boundary, like `^A`/`^E`).
  - **Shift-selection**: Shift+Left/Right, Shift+Alt+Left/Right, Shift+Cmd+Left/Right extend/shrink the selection using an anchor model (`cursoratq1` field on `Window`).
  - **Command history**: Ctrl+P (previous) / Ctrl+N (next) browse per-window history (256 entries, no consecutive duplicates). Enter records and exits browsing. History is in-memory only.
- **`win` enhancements** (`src/cmd/9term/win.c`): same Ctrl+P/Ctrl+N history as 9term. Acme intercepts most special keys before `win` sees them; Ctrl+P/Ctrl+N are plain control characters that Acme passes through. Acme inserts the character into the body before sending the `'K'` `'I'` event — `win` accounts for this by bumping `ntyper`/`ntypeb` by `e.nr`/`e.nb` before calling `histreplace`, so the delete range covers the inserted control character.

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

### `plumb/basic` and `~/lib/plumbing`

This fork aligns with upstream's `plumb client $editor` pattern. `$editor` is
set to `plumb-edit` in `~/lib/plumbing`. The port declarations are still required
because `plumb client` only registers the port dynamically when a client connects,
and the plumber needs the port declared up-front:

```
# declarations of ports without rules
plumb to edit
plumb to seemail
plumb to showmail
```

**File routing**: `~/lib/plumbing` includes `plumb/basic` and `plumb/macos`.
The order matters — `include macos` must come **before** the general `(.+?)`
editor catch-all, because that catch-all matches any existing file (including
PDFs, images, etc.) and would prevent the macOS rules from ever firing.

**Filename regexes**: all `data matches` guards in `plumb/basic` use `.+` (not
restrictive character classes) so any filename — including spaces, braces, etc.
— matches. `arg isfile $0` then verifies the file actually exists on disk.

**Plumber parse rules** (from `src/cmd/plumb/rules.c`):
- A ruleset may have at most one `plumb to X` (sets the destination port).
- A ruleset may have at most one `plumb start` or `plumb client` action.
- Having both `plumb to X` and `plumb start Y` in the same ruleset is **valid**.
- Having two `plumb to` lines in one ruleset causes "too many ports" error.
- `buildargv` splits `$file` on spaces into multiple argv elements. Use
  `p9p-open` (which reassembles them) rather than `open` directly.
- Single-quoted `'$file'` in a rule **prevents** `$file` expansion — the plumber
  treats single quotes as quoting, not as shell quoting. Use bare `$file`.

### `bin/plumb-edit`

Called by the plumber via `plumb client $editor` when a file match fires. Starts
acme if needed (waits for `9p stat acme`), then sends `plumb -d edit $file`
directly to acme's edit port. Using `-d edit` bypasses rule re-evaluation and
prevents recursive invocation.

### `bin/p9p-open`

Wrapper around `open -b <bundleID>` used by `plumb/macos` rules. The plumber's
`buildargv` splits `$file` on spaces into separate argv elements; `p9p-open`
reassembles everything after `-b <bundleID>` into a single path with spaces.

Also has a test hook: if `/tmp/plumb-test-logpath` exists and contains a path,
it appends `bundleID<TAB>file` to that path and touches `<path>.done`. The test
runner in `tests/plumb/run-tests` uses this to verify routing without opening
actual apps.

### `Plumb.app` and `bin/macedit`

`Plumb.app` is the macOS default handler for files double-clicked in Finder. Its
launcher calls `macargv` to receive file paths, then passes each to `plumb`
directly. The plumber routes each file to the appropriate destination.

Upstream's `bin/macedit` (which `Plumb.app` used to call) forced all files to
`plumb -d edit`, bypassing routing rules, and had a special workaround for
filenames with spaces (sending file *content* as inline data). This fork removes
`macedit` because: (1) `.+` regexes handle any filename, (2) `p9p-open`
reassembles space-split paths, and (3) routing to native macOS apps (Preview,
QuickTime, etc.) is desirable. See `mac/MacOS.md` for the full explanation.

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
| acme reopens continuously after closing | `plumb-edit` calling `plumb $file` without `-d`, re-triggering rules | Use `plumb -d edit $file` in `plumb-edit` |
| PDF/image/audio opens in acme instead of native app | `include macos` placed after the `(.+?)` editor catch-all in `~/lib/plumbing` | Move `include macos` **before** the catch-all; macos rules use specific extensions so source files still fall through |
| `plumb start p9p-open -b com.apple.Preview '$file'` passes literal `$file` string | Single quotes in plumber rules prevent `$file` expansion (plumber quoting ≠ shell quoting) | Use bare `$file` without quotes; use `p9p-open` to reassemble space-split argv |
| `plumb /path/with spaces/file.pdf` fails or opens wrong app | `buildargv` splits `$file` on spaces into multiple argv elements | Use `p9p-open` as the command in `plumb start`; it joins all args after `-b <bundleID>` back into one path |
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
| `win` history: stray control character printed after Ctrl+P/Ctrl+N | Acme inserts the character into the body before sending the `'K'` `'I'` event; `win` breaks before `type()` so `ntyper` doesn't include it, but the body does | Bump `ntyper += e.nr; ntypeb += e.nb` before calling `histreplace`; the delete range `[q.p, q.p+ntyper)` then covers the inserted character |
| `win` history: stray character printed when Ctrl+N with no forward history | `break` without consuming the already-inserted `^N` | Always bump `ntyper`/`ntypeb` and call `histreplace` even when not browsing; pass `typing, ntypeb - e.nb` to restore the pre-`^N` state |
| `Kscrolloneup`/`Kscrollonedown` conflict with `Kshiftaltright`/`Kcmdleft` | 9term's `dat.h` defined scroll constants at `KF\|0x20` and `KF\|0x21`, which collide with `keyboard.h` values added later | Renumbered to `KF\|0x26` and `KF\|0x27`; safe because these are only sent internally via `wkeyctl(w, Kscrolloneup)`, never from the keyboard driver |
| `cannot refer to declaration with an array type inside block` in Objective-C block | C-style stack arrays cannot be captured by blocks | Heap-allocate with `malloc`/`strdup`, `free` after use inside the block |
| Dock menu shows app name for all windows instead of file paths | `setlabel:` was setting `[win setTitle:label]`, making window title and Dock label the same | Set `[win setTitle:bundleName]` for the title bar; store full label separately in `winreg_pending_title` for the Dock menu |
