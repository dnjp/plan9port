# macOS Integration

This directory contains macOS-specific integration for plan9port: `.app` bundles
for the GUI applications and `LaunchAgent` plists for the background daemons.

## App Bundles

`9term.app`, `Acme.app`, `Sam.app`, and `Plumb.app` are standard macOS
application bundles. Install them by running `mk` (or `mk apps`) from this
directory, which copies them to `/Applications`.

## Architecture

### Single-process multi-window model

plan9port's GUI applications (`9term`, `acme`, `sam`) draw to the screen
through a subprocess called `devdraw`. On macOS, `devdraw` is the actual
`NSApplication` — the process that owns the Dock icon, menu bar, and window
chrome.

This fork runs a single long-lived `devdraw` process per application. All
windows for that application live as `NSWindow` instances inside that one
process:

```
9term.app launch
  └─ devdraw -s p9p-9term   (NSApplication — one Dock icon, one menu bar)
       ├─ NSWindow #1  ←  9term client process (pid A)
       ├─ NSWindow #2  ←  9term client process (pid B)
       └─ NSWindow #3  ←  9term client process (pid C)
```

Each client process (`9term`, `acme`, `sam`) connects to the running `devdraw`
server via a Unix socket in `$NAMESPACE` rather than forking its own `devdraw`.
The connection is established through the `$wsysid` environment variable, which
`libdraw` reads in `_displayconnect()` to dial the server instead of spawning a
new display process.

This model gives native macOS behaviour for free: `Cmd+\`` window cycling, a
single Dock icon, a single menu bar, and native tab support in `9term` — all
without any cross-process IPC hacks.

### Startup sequence

1. macOS launches `devdraw` directly (it is the `CFBundleExecutable`). `devdraw`
   detects it is running from an app bundle, sets `srvname = "p9p-9term"` (or
   the appropriate name for the bundle), and starts listening on
   `$NAMESPACE/p9p-9term`.

2. `devdraw` queues a `newWindow:` call via `dispatch_async` and enters
   `[NSApp run]`.

3. `newWindow:` spawns the client binary (e.g. `$PLAN9/bin/9term`) with
   `$wsysid = p9p-9term/<id>` in its environment. The client dials the server,
   and `devdraw` creates an `NSWindow` for it via `rpc_attach`.

4. Subsequent "New Window" (Cmd+N) or "New Tab" (Cmd+T, 9term only) calls
   repeat step 3 with a fresh id.

### Window lifecycle

- **Client exits normally** (e.g. shell exits): `serveproc` sees EOF on the
  socket and calls `rpc_clientgone`, which dispatches to the main thread,
  closes the `NSWindow`, and frees the client state.

- **User closes the window** (click ✕ or Cmd+W): `windowWillClose:` fires,
  sets `clientGone = YES` to stop pending draw operations, and sends `SIGTERM`
  to the client process. When the client dies, `rpc_clientgone` runs as above.

- **Last window closed**: `applicationShouldTerminateAfterLastWindowClosed:`
  returns `YES`; the app exits.

- **App quit** (Cmd+Q or Dock menu): `applicationShouldTerminate:` sends
  `SIGTERM` to all tracked child PIDs, then returns `NSTerminateNow`.

### Memory management

`DrawView` (the `NSView` subclass that owns each window's drawing surface) is
kept alive by an explicit `CFRetain` in a registry table rather than by ARC
alone. This avoids a double-free caused by `NSWindow` autoreleasing its content
view into the run-loop pool during close: the registry's `CFRetain` keeps the
object alive until `rpc_clientgone` drops it on a future run-loop turn, after
all pool entries from the close event have drained.

## App bundle structure

```
9term.app/
  Contents/
    Info.plist          — CFBundleExecutable=devdraw, CFBundleName=9term
    MacOS/
      devdraw           — copy of $PLAN9/bin/devdraw (placed here by mk apps)
      9term             — helper script used by acme/sam/plumb (not the executable)
    Resources/
      spaceglenda.icns  — application icon
```

`Acme.app` and `Sam.app` follow the same pattern. `CFBundleExecutable` is
`devdraw` in all three; the bundle name (`CFBundleName`) determines which client
binary is spawned and which features are enabled (e.g. tabs are only available
in `9term`).

### Why `devdraw` is the bundle executable

macOS associates a process with a bundle by matching the process's executable
path to `Contents/MacOS/<CFBundleExecutable>`. By making `devdraw` the bundle
executable, `[NSBundle mainBundle]` returns the correct bundle at runtime,
giving `devdraw` access to the right icon, display name, and bundle identifier —
without any wrapper script.

### Bundle-internal `devdraw` copy

`mk apps` copies `$PLAN9/bin/devdraw` into each bundle's `Contents/MacOS/`
directory. Each bundle gets its own copy so that macOS correctly associates the
running process with that bundle (icon, app name, Dock entry).

### Code signing

Copying `devdraw` into the bundle invalidates the bundle's code signature.
`mk apps` re-signs the entire bundle with:

```sh
codesign --force --deep --sign - /Applications/9term.app
```

The `--deep` flag is required. Without it, macOS will `SIGKILL` the process at
launch with "Code Signature Invalid".

## Building and installing

```sh
mk           # build devdraw and install all apps + daemons
mk apps      # build devdraw and install only the .app bundles
mk daemons   # install LaunchAgent plists
mk load      # start all LaunchAgents
mk unload    # stop all LaunchAgents
mk reload    # restart all LaunchAgents
mk status    # show running/stopped state
mk logs      # tail all daemon log files
```

## macOS-specific features

### 9term tabs

`9term.app` supports native macOS tabs:

- **New Tab** (Cmd+T): opens a new `9term` in a tab within the current window.
- **New Window** (Cmd+N): opens a new `9term` in a separate window.
- Tabs can be dragged out to become separate windows and vice versa using
  standard macOS tab bar controls.

`Acme.app` and `Sam.app` do not support tabs; each window is always separate.

### Window titles

The title bar always shows the application name (`9term`, `acme`, or `sam`)
regardless of the label set by the client via `drawsetlabel`. This keeps the
title bar stable while the client updates its label (e.g. acme updates the
label to the focused file path).

### Mouse buttons

On macOS, because a laptop trackpad has only one physical button:

- Option-click → button 2
- Command-click → button 3

While the main button is held down, Control, Option, and Command simulate
buttons 1, 2, and 3 for chording in acme. For example, the 1-3 paste chord can
be executed by sweeping with the trackpad button held, then pressing Command.

Buttons 4 and 5 represent scroll wheel up/down. Two-finger trackpad scrolling
sends those events.

Holding Shift while clicking adds 5 to the button number (e.g.
Command-Shift-Click = button 8). Acme interprets button 8 as reverse search.

### Keyboard shortcuts

- **Cmd+F**: toggle full screen
- **Cmd+R**: toggle retina mode (for debugging)
- **Cmd+N**: new window
- **Cmd+T**: new tab (9term only)
- **Cmd+W**: close window

All other Command key combinations send `Kcmd` (0xF100) plus the character to
the client. Acme recognises Cmd+Z (undo), Cmd+Shift+Z (redo), Cmd+X (cut), and
Cmd+V (paste).

## Plumb.app and file routing

`Plumb.app` is a minimal bundle whose sole purpose is to act as the macOS
default handler for files double-clicked in Finder. Its launcher
(`Contents/MacOS/plumb`) calls `macargv` to receive the file paths from Finder,
then passes each to the `plumb` client:

```sh
for file in $($bin/macargv); do
    "$bin/plumb" "$file"
done
```

The plumber daemon applies the rules in `~/lib/plumbing` (which includes
`plumb/basic` and `plumb/macos`) to route each file — opening source files in
acme, PDFs in Preview, audio in QuickTime Player, etc.

Register `Plumb.app` as the default handler for text and source files:

```sh
mk register-defaults   # requires: brew install duti
```

## Background daemons

The `daemons/` subdirectory contains `LaunchAgent` plist templates and helper
scripts for the three plan9port background services:

| Service | Purpose |
|---------|---------|
| `com.plan9port.fontsrv` | Serves system fonts over 9P at `/mnt/font` |
| `com.plan9port.plumber` | Routes plumb messages between applications |
| `com.plan9port.ruler` | Configuration rule server (font, tabstop, etc.) |

Plist templates live in `daemons/*.plist.template`. `mk daemons` substitutes
`@HOME@`, `@PLAN9@`, and `@DAEMONDIR@` and writes the expanded plists to
`~/Library/LaunchAgents/`.

### `run-with-env.sh`

LaunchAgents start in a minimal environment without the user's login profile.
`run-with-env.sh` is the actual program each plist runs; it sources the user's
shell profile before exec'ing the daemon binary so that `$PLAN9`, `$NAMESPACE`,
and other required variables are set correctly.

### Shared namespace

All daemons and interactive terminals must share the same 9P socket directory
(`$NAMESPACE`, typically `/tmp/ns.$USER.:0`). `run-with-env.sh` sets
`NAMESPACE` explicitly before starting each daemon to ensure this even when the
LaunchAgent fires before the user's first terminal session.
