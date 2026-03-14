# macOS Integration

This directory contains macOS-specific integration for plan9port: `.app` bundles
for the GUI applications and `LaunchAgent` plists for the background daemons.

## App Bundles

`Acme.app`, `9term.app`, `Sam.app`, and `Plumb.app` are standard macOS application
bundles. Install them by running `mk` (or `mk apps`) from this directory, which
copies them to `/Applications`.

### How the app bundles work

plan9port's GUI applications (acme, sam, 9term) are not native macOS apps. When
they need to draw to the screen they fork a subprocess called `devdraw`, which is
the actual `NSApplication` process — the one that owns the Dock icon, menu bar
entry, and window chrome.

Without intervention, `devdraw` runs as a standalone binary with no bundle
association, so macOS shows a generic Glenda icon and labels it "devdraw" in the
Dock. To fix this, each app bundle uses two mechanisms:

1. **Bundle-internal `devdraw` copy.** `mk apps` copies `$PLAN9/bin/devdraw` into
   each bundle's `Contents/MacOS/` directory. When `devdraw` runs from that path,
   `[NSBundle mainBundle]` returns the enclosing `.app` bundle, giving macOS the
   correct bundle identifier, icon, and display name.

2. **`DEVDRAW` environment variable.** Each bundle's launcher script
   (`Contents/MacOS/acme`, `9term`, `sam`) sets:
   ```sh
   export DEVDRAW="$(dirname "$0")/devdraw"
   ```
   `libdraw` reads `$DEVDRAW` when forking the display server, so it execs the
   bundle-internal copy rather than the bare `devdraw` on `$PATH`.

3. **Bundle-aware icon and name in `devdraw`.** `mac-screen.m` reads
   `CFBundleName` and `CFBundleIconFile` from the main bundle at startup. If a
   bundle icon is found it is used instead of the hardcoded Glenda PNG, and the
   menu bar title is set to the bundle's app name rather than "devdraw".

The net result: launching `Acme.app` shows the Acme icon in the Dock, names the
menu bar "acme", and keeps the app active under that identity for its lifetime.

### Bundle structure

```
Acme.app/
  Contents/
    Info.plist          — CFBundleName=acme, CFBundleIconFile=spaceglenda.icns
    MacOS/
      acme              — launcher script (CFBundleExecutable); sets DEVDRAW and execs $PLAN9/bin/acme
      devdraw           — copy of $PLAN9/bin/devdraw placed here by mk apps
    Resources/
      spaceglenda.icns  — application icon
```

`9term.app` and `Sam.app` follow the same pattern with their own icons and
launcher scripts.

### Launcher scripts

Each launcher script (`Contents/MacOS/acme`, `9term`, `sam`) does the following:

1. Sets `INSIDE_P9P=true` and an app-specific flag (`INSIDE_ACME`, `INSIDE_9TERM`).
2. Sets `PLAN9` (defaulting to `/usr/local/plan9`) and prepends `$PLAN9/bin` to
   `PATH`. This is required because Dock-launched apps inherit a minimal `PATH`
   that does not include the plan9port binaries (e.g. `9pserve`).
3. Sets `DEVDRAW` to the bundle-internal copy.
4. Execs the plan9port binary (e.g. `$PLAN9/bin/acme`).

### Plumb.app and file routing

`Plumb.app` is a minimal app bundle whose sole purpose is to act as the macOS
default handler for files double-clicked in Finder. Its launcher
(`Contents/MacOS/plumb`) calls `macargv` to receive the file paths from Finder,
then passes each path to `plumb` (the plan9port plumb client):

```sh
for file in $($bin/macargv); do
    "$bin/plumb" "$file"
done
```

The plumber daemon then applies the rules in `~/lib/plumbing` (which includes
`plumb/basic` and `plumb/macos`) to decide what to do with each file — opening
source files in acme, PDFs in Preview, audio in QuickTime Player, etc.

#### Why `macedit` was removed

Upstream plan9port ships a helper script `bin/macedit` that `Plumb.app` used to
call instead of `plumb` directly. It existed to work around two limitations:

1. **Forced editor destination** — it called `plumb -d edit "$file"`, which
   bypasses the plumber's routing rules and sends the file directly to the `edit`
   port (acme). This meant every file opened via Finder went to acme, regardless
   of type.

2. **Spaces in filenames** — the plumbing rules used restrictive character-class
   regexes (e.g. `[a-zA-Z0-9_\-./@ ]+`) that couldn't match filenames with
   braces, parentheses, or other special characters. For filenames containing
   spaces, `macedit` worked around this by reading the file's *content* and
   sending it as inline plumb data with a mangled filename
   (`/BadName/name_with_underscores`).

Both workarounds are no longer needed in this fork:

- `plumb/basic` and `plumb/macos` use `.+` guard patterns that accept any
  filename, and `p9p-open` reassembles space-split paths from `buildargv`.
- The routing rules now correctly dispatch PDFs, images, audio, etc. to their
  native macOS apps, so forcing `-d edit` would be wrong.

`macedit` was therefore reduced to a single `plumb "$1"` call and then removed.
If you need to re-sync with upstream, be aware that upstream's `macedit` forces
all Finder-opened files to the editor — you would lose the macOS app routing.

### Code signing

Copying `devdraw` into the bundle invalidates the bundle's code signature.
`mk apps` re-signs the entire bundle with:

```sh
codesign --force --deep --sign - /Applications/Acme.app
```

The `--deep` flag is required — signing only the top-level bundle or only the
binary is not sufficient. Without it, macOS will `SIGKILL` the process at launch
with "Code Signature Invalid".

### Multi-window support and Dock menu

plan9port apps are single-process-per-window: each "New Window" spawns a
completely separate process. To present a single Dock icon for all windows of
the same app, `devdraw` uses a primary/secondary model with Unix domain socket
IPC.

#### Primary vs. secondary instances

- The **primary** instance is the first one launched (no `P9P_SECONDARY` env
  var). It sets `NSApplicationActivationPolicyRegular` (owns the Dock icon and
  menu bar) and starts a Unix domain socket server at
  `/tmp/p9p-winreg-<bundleID>`.
- **Secondary** instances are spawned by "New Window" via
  `open -n --env P9P_SECONDARY=1 <bundle>`. They set
  `NSApplicationActivationPolicyAccessory` (no Dock icon) and connect to the
  primary's socket.

#### IPC protocol

Secondaries send newline-terminated messages to the primary:

| Message | Meaning |
|---------|---------|
| `title <pid> <label>\n` | Register or update this window's title |
| `close <pid>\n` | This window is closing |

The primary stores these in a `WinEntry winreg[]` array (max 64 entries) and
uses it to populate the Dock right-click menu.

#### Dock right-click menu (`applicationDockMenu:`)

The primary builds the menu from two sources:

1. **Local windows** (`[NSApp windows]`): uses `winreg_pending_title` (the full
   path label) for the menu item text, falling back to `[win title]`. Action:
   `bringWindowToFront:` (brings the `NSWindow` to front).
2. **Remote windows** (secondary instances in `winreg[]`): uses the registered
   title. Action: `activateSecondaryWindow:` (calls
   `[NSRunningApplication activateWithOptions:]` for the secondary's PID).

#### Window title bar vs. Dock menu label

The macOS title bar always shows the `CFBundleName` (e.g. "acme") for
stability. The full path label (set by `drawsetlabel`) is stored separately in
`winreg_pending_title` and used only for the Dock menu. This decoupling is done
in `setlabel:` in `mac-screen.m`:

```objc
NSString *bundleName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
[self.win setTitle:bundleName ?: s];          // title bar: always "acme"
strlcpy(winreg_pending_title, label, ...);    // dock menu: full path
```

#### Primary promotion

When the primary exits, secondaries detect the socket closure (the blocking
`read` loop in `winreg_connect` returns 0). The first secondary to detect this
calls `winreg_promote()` on the main thread, which:

1. Starts a new socket server (becoming the new primary).
2. Sets `NSApplicationActivationPolicyRegular`.
3. After a 200 ms delay (required for the Dock to pick up the policy change),
   calls `[NSApp activateIgnoringOtherApps:YES]`.

#### libc.h macro conflicts

plan9port's `libc.h` redefines `accept`, `listen`, `close`, and `write` to
`p9accept`, `p9listen`, `p9close`, and `p9write`. The IPC code in
`mac-screen.m` needs the real POSIX functions. The file `#undef`s these macros
before the IPC section, declares the POSIX prototypes explicitly, then
`#define`s them back to the plan9port names afterwards.

### Acme window labels

Acme calls `drawsetlabel()` in two places to keep the Dock menu titles
meaningful:

1. **At startup** (`acme.c: threadmain`): sets the label to the current working
   directory (`wdir`).
2. **On focus change** (`acme.c: mousethread`): when a window gains focus, sets
   the label to `w->body.file->name` (the file path of the focused buffer).

## Background Daemons

The `daemons/` subdirectory contains `LaunchAgent` plist templates and helper
scripts for the three plan9port background services:

| Service | Purpose |
|---------|---------|
| `com.plan9port.fontsrv` | Serves system fonts over 9P at `/mnt/font` |
| `com.plan9port.plumber` | Routes plumb messages between applications |
| `com.plan9port.ruler` | Configuration rule server (font, tabstop, etc.) |

### Installation and management

```sh
mk daemons   # expand plist templates and install LaunchAgents
mk load      # launchctl load (start) all agents
mk unload    # launchctl unload (stop) all agents
mk reload    # stop then start (pick up plist changes)
mk status    # show running/stopped state of each agent
mk logs      # tail all daemon log files
```

Plist templates live in `daemons/*.plist.template`. `mk daemons` substitutes
`@HOME@`, `@PLAN9@`, and `@DAEMONDIR@` and writes the expanded plists to
`~/Library/LaunchAgents/`.

### `run-with-env.sh`

LaunchAgents start in a minimal environment without the user's login profile.
`run-with-env.sh` is the actual program each plist runs; it sources the user's
shell profile before exec'ing the daemon binary so that `$PLAN9`, `$NAMESPACE`,
and other required variables are set correctly.

### Namespace

All daemons and interactive terminals must share the same 9P socket directory
(`$NAMESPACE`, typically `/tmp/ns.$USER.:0`). `run-with-env.sh` sets
`NAMESPACE` explicitly before starting each daemon to ensure this even when the
LaunchAgent fires before the user's first terminal session.
