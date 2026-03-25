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

### Tag path and Get

- **When the path is applied**: The tag's left part (file/dir path) is not applied to the
  window's file name on every commit; it is applied only when the user runs **Get** (or
  when the file is opened via Look/plumb). So typing a path in the tag does not contract
  it until Get is run.
- **Get with no argument**: When the tag has a path, Get uses that path (so renaming the
  tag then running Get loads the new path). When the tag has no path (empty left part),
  Get re-loads the current file path. When getarg returns the command word (e.g. "Get")
  rather than a path, `getname` promotes and uses the tag path or current file path as above.
- **Dump/Load**: Paths are expanded with `expandhome_c()` so `~/acme.dump` works.
- **Event path**: When Get is run via the 9P event interface (e.g. menu), `argt` is set to
  `&w->tag` in `xfideventwrite`; when `argt` is nil in `get()`, it is set to `&w->tag` so
  the tag is always available for parsing.

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

### Single-process multi-window model

Each app bundle runs a single long-lived `devdraw` process. All windows for that
app live as `NSWindow` instances inside that one process. `devdraw` acts as a 9P
server; each client (`acme`, `9term`, `sam`) connects via a Unix socket using the
`$wsysid` environment variable. This gives a single Dock icon, single menu bar,
and native Cmd+` window cycling.

### Window / DrawView lifecycle

`DrawView` is the `NSView` subclass that owns each window's drawing surface.
There are two teardown paths:

**Path A — User closes window (✕ / Cmd+W)**:
1. `windowWillClose:` sets `clientGone=YES` and `self.win=nil` (releases ARC
   retain while AppKit still holds its own internal retain).
2. `windowWillClose:` closes the client socket fd (or sends SIGTERM to 9term
   child). `serveproc` sees EOF and calls `rpc_clientgone`.
3. `rpc_clientgone` (dispatched to main thread) finds `clientGone=YES`, nils
   remaining properties, frees `Client`. `view.win` is already nil — no
   double-release.

**Path B — Client exits on its own**:
1. `serveproc` sees EOF, calls `rpc_clientgone`.
2. `rpc_clientgone` (main thread) finds `clientGone=NO`, removes observer,
   calls `[view.win orderOut:nil]`, then sets `view.win=nil`.

**`isReleasedWhenClosed=NO` is mandatory.** `NSWindow` defaults to
`isReleasedWhenClosed=YES` — a legacy pre-ARC behaviour where AppKit fires an
extra `-release` on close. Combined with `DrawView`'s ARC `retain` property on
`win`, this causes a double-free crash (`*** -[NSWindow release]: message sent to
deallocated instance`). Always set `[win setReleasedWhenClosed:NO]` when creating
windows in `mac-screen.m`.

### viewRegistry

`viewRegistry` maps raw `void*` client view pointers to `CFRetain`'d `DrawView`
references. This allows background libthread threads to safely look up a view by
pointer and dispatch work to the main thread without holding an ARC reference
across thread boundaries.

Ownership rules:
- `viewRegistry_add`: `CFRetain`s the view (registry owns +1).
- `viewRegistry_getref`: returns a +1 `CFRetain`'d reference; caller must
  transfer to ARC via `__bridge_transfer` inside a `dispatch_async` block on the
  main thread (never on the RPC thread — no autorelease pool there).
- `viewRegistry_remove`: `CFRelease`s the registry's +1. Must only be called
  from the main thread, after `clientGone` is set.

### Tracing / debugging

Verbose trace logging is compiled out by default. To re-enable, define
`DEVDRAW_TRACE` at compile time:

```sh
CC9FLAGS="-DDEVDRAW_TRACE" mk install
```

Traces go to `/tmp/devdraw-trace.log`.

## Shell environment

Files in `shell/` ending in `.sh.in`, `.fish.in`, `.rc.in` are templates.
`SETUP` processes them with `sed "s|/usr/local/plan9|$PLAN9|g"` and installs
them via `mk install` (no sudo — `/usr/local/plan9` is user-owned in this fork).

**Always edit the `.in` files.** The generated files (`plan9.sh`, `p9p-session.sh`,
etc.) are produced at install time and should not be committed.

**PATH ordering**: `plan9.sh` prepends `$PLAN9/bin` to `PATH`. For this to take
effect, `plan9.sh` must be sourced **last** in `~/.profile`, after all other tools
(homebrew, asdf, go, tfenv, etc.) have added their entries. If sourced early,
those tools will prepend over it and plan9 executables will lose priority.

**`shell/mkfile`**: the `install` target uses `cmp -s` before `cp` to avoid
errors when source and destination are identical (macOS `cp` errors on same-file
copy). The `install-sudo` target has been removed — it is no longer needed.

## `src/cmd/walk/` — walk, field, sor

`walk` is a Plan 9-style `find` replacement. `field` extracts tabular fields.
`sor` (select on rc) filters filenames from stdin using rc snippet expressions.

### sor design

`sor` reads filenames from stdin via `9 read` (plan9port's `read` binary, not a
shell builtin) and for each filename runs a set of rc snippet tests.

**Snippet convention**: snippets receive the filename as `$1` (positional arg).
Example: `walk | sor '~ $1 *.go'`.

**`eval` is broken for this use case in plan9port rc.** The original Plan 9 `sor`
used `eval $1 ''''$file''''` inside `runtests`. This worked in Plan 9 rc because
`eval` set `$*` from its extra arguments (making `$1` inside the eval'd code equal
to the filename). Plan9port's `eval` does NOT do this — it concatenates all
arguments with spaces and re-parses in the current scope. So `$1` inside the
eval'd string remains the outer `$1` (the snippet text), not the filename.

The specific failure mode: with snippet `~ $1 *.go` and file `foo.c`, eval runs
`~ '~ $1 *.go' *.go foo.c` — and the snippet text `~ $1 *.go` itself matches the
`*.go` pattern, so the test always returns true regardless of the filename.

**Current workaround**: `sor` is an rc script that uses `9 read` for streaming
input. The `eval` bug means it does not currently work correctly with glob
snippets. The proper fix is to patch `eval` in plan9port's `src/cmd/rc/exec.c`
to set `$*` from extra arguments as original Plan 9 rc did.

### `src/cmd/walk/mkfile` conventions

- Local build outputs are named `o.<name>` (e.g. `o.walk`, `o.field`) so the
  standard `o.*` gitignore rule excludes them.
- Object files use `o.<name>.o` as an intermediate name, also caught by `o.*`.
- `sor` is an rc script — no build step, installed directly with `install -m 755`.

### plan9port rc gotchas

| Behaviour | Plan 9 rc | Plan9port rc |
|-----------|-----------|--------------|
| `read` in a loop | builtin; returns false at EOF, loop terminates cleanly | external binary at `$PLAN9/bin/read`; exits 1 at EOF but `` `{read} `` at EOF returns empty list `()`, causing "null list in concatenation" error in `while(var = `{read})` |
| `eval cmd arg` | sets `$1=arg` for the eval'd code | concatenates all args with spaces; `$1` inside eval is the outer `$1`, not the new arg |
| `break` | builtin | not a builtin — `break: No such file or directory` |
| `while` loop streaming | `while(line = `{read})` works | use `for (line in `{cat})` to buffer all input, or a recursive function |

**Streaming lines in plan9port rc**: use `while (var = `{9 read})` — calling
`9 read` (the plan9port binary via the `9` wrapper) works correctly because `9`
ensures `$PLAN9/bin/read` is used. The `while` loop terminates when `read` exits
non-zero at EOF and the assignment returns an empty list (rc treats a failed
command substitution in a `while` condition as loop termination, not an error,
when the command itself exits non-zero).

**Why `9 read` and not just `read`**: without `$PLAN9/bin` taking precedence in
`PATH`, `read` may resolve to `/bin/read` (macOS) which has different behaviour.
Always use `9 read` in rc scripts that need plan9port's `read`.

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
| `Put`/`acme/put` emits `~/...` paths instead of `/Users/...` | `putfile()` could receive the contracted form and pass it directly to OS `create()` | Expand `~`/`$HOME` inside `putfile()` before `create()` so OS sees an absolute external path |
| `sor '~ $1 *.go'` matches every file regardless of extension | `eval` in plan9port rc does not set `$*` from extra args; snippet text `~ $1 *.go` ends in `.go` and matches its own pattern | Fix `eval` in `src/cmd/rc/exec.c` to set `$*` from extra arguments as original Plan 9 rc did |
| `while(file = `{read})` loop never terminates or errors with "null list in concatenation" | `read` resolves to macOS `/bin/read` or plan9port's external `read` binary; at EOF it exits non-zero and `` `{read} `` returns `()`, which errors on assignment | Use `while (file = `{9 read})` to force plan9port's `read`, or `for (file in `{cat})` to buffer all input first |
| Plan9 executables not taking precedence over system tools (e.g. wrong `read` used) | `plan9.sh` is sourced early in `~/.profile`; later tools (homebrew, asdf, etc.) prepend to `PATH` and bury `$PLAN9/bin` | Source `plan9.sh` **last** in `~/.profile`, after all other PATH-modifying tool setup |
| `mk install` in `shell/` fails with "are identical (not copied)" | macOS `cp` errors when source and destination are the same file | Use `cmp -s src dst \|\| cp src dst` pattern in the install loop |
| `field.c` fails to compile: `match[0].sp` / `match[0].ep` not found | plan9port's `regexp9.h` uses nested unions: `Resub.s.sp` / `Resub.e.ep`, not flat `.sp` / `.ep` | Change to `match[0].s.sp` and `match[0].e.ep` |
| `field.c` warning: passing `const char*` to `utflen(char*)` | `insep` is `const char*` but `utflen` takes `char*` | Cast: `utflen((char*)sep)` |
| `*** -[NSWindow release]: message sent to deallocated instance` on window close | `NSWindow.isReleasedWhenClosed` defaults to `YES`; AppKit fires an extra `-release` on close which double-frees the window when `DrawView.win` (a `retain` property) is also released | Set `[win setReleasedWhenClosed:NO]` immediately after creating every `NSWindow` in `mac-screen.m` |
| Window close crashes even after `self.win = nil` in `windowWillClose:` | `rpc_clientgone`'s `__bridge_transfer` ran while AppKit's autorelease pool still held a pending release on the window | Ensure `viewRegistry_remove` + `__bridge_transfer` happen inside a `dispatch_async` block on the main thread wrapped in `@autoreleasepool`, never on the RPC thread |
| plumber / `editinacme` can't find acme when launched from Acme.app | `bin/acme` was creating a random `$NAMESPACE` (`/tmp/ns.$USER.$$`) for every devdraw-managed window, so acme's 9P service never landed in the shared namespace | `newWindow:` in `mac-screen.m` now assigns `NAMESPACE`: cid=0 gets the shared namespace (`:0`), cid>0 gets a predictable derived namespace (`:1`, `:2`, …). `bin/acme` no longer overrides `$NAMESPACE` when `$wsysid` is set |
| `9p stat acme` fails even though Acme.app is running | acme posted its 9P service in a random namespace, not the shared one | See above — first window always uses the shared namespace |
| acme "Illegal instruction: 4" when typing a path like `/Users/daniel` in the tag | Infinite recursion: `wincommit` parses tag (raw path), calls `winsetname` → `filesetname` contracts name (e.g. to `~`) → `winsettag` → `winsettag1` → `wincommit` again; tag buffer still has raw path so comparison with `file->name` (`~`) fails and we loop until stack/malloc blows up | In `wincommit`, compare **contracted** parsed tag name to `w->body.file->name`; if equal, return without calling `winsetname`/`winsettag` so the cycle is broken |
| Get after renaming tag still loads old directory | In promote path, file name was used before tag; so tag path was ignored when file had a name | In `getname`, when promote and n<=0, use **tag path first** if tag has a non-empty left part; only fall back to file->ename/file->name when tag is empty |
| `win` in acme: tag doesn't update when shell runs `awd` (e.g. on `cd` via p9p-session) | `winsettag1` preserves the tag's left part when it doesn't match `file->name` (to avoid overwriting user typing). After a ctl "name" write from win/awd, the tag still showed the previous path, so we preserved that instead of the new one | When both the current tag and `file->name` look like win directory labels (path/-sysname), treat as awd update and use `file->name` in the tag. Added `runehasdirlabel()` (true if string contains "/-") and use it in `winsettag1` so ctl "name" updates from win take effect |
