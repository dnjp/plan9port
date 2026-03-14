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
      acme-exec         — thin wrapper used by the acme wrapper script for argv[0] naming
      devdraw           — copy of $PLAN9/bin/devdraw placed here by mk apps
    Resources/
      spaceglenda.icns  — application icon
```

`9term.app` and `Sam.app` follow the same pattern with their own icons and
launcher scripts.

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
