# Devdraw Multi-Window Architecture

## The Problem

Every `9term` window is a separate OS process. When "New Window" is invoked,
the launcher script spawns a new `9term` binary, which calls `initdraw()`, which
forks a new `devdraw` process. Each `devdraw` is a full `NSApplication` instance.

```
9term.app launch
  └─ 9term binary
       └─ devdraw (NSApplication #1, Dock icon, menu bar owner)

"New Window"
  └─ 9term binary
       └─ devdraw (NSApplication #2, separate process)
```

macOS assigns one Dock icon per `NSApplication`. There is no supported way to
have a secondary `NSApplication` share a Dock icon with another, or to have a
window in process B appear as a window of process A's menu bar. Everything we
have built in `mac-screen.m` — the `winreg` socket server, the `Accessory`
policy toggling, the `activateprimary` IPC message — is fighting this
constraint.

## Prior Art

### devdrawserver (Marius Eriksen, ~2012)

[github.com/mariusae/devdrawserver](https://github.com/mariusae/devdrawserver)
— described as "a server version of p9p's devdraw — sadly needed as it's not a
real file server." It allows a single devdraw to serve multiple clients over a
Unix socket, primarily for remote/SSH use cases. It does not address the macOS
multi-window problem because it still results in one `NSApplication` per
`devdraw` instance; it just moves the socket connection off pipes.

### devdraw `-s` server mode (already in plan9port)

`devdraw.c` already contains a server mode activated by `-s <name>`. When run
with `-s`, `devdraw` calls `announce()` on a Unix socket at
`$NAMESPACE/<name>` and accepts multiple client connections via `listenproc`,
allocating a fresh `Client` struct for each. Each client gets its own
`NSWindow` via `rpc_attach()`. The `wsysid` environment variable in
`libdraw/drawclient.c` is the client-side hook: if `$wsysid` is set to
`<srvname>/<id>`, `_displayconnect()` dials the running server instead of
forking a new `devdraw`.

This is the mechanism we need. It is already fully implemented in the C layer.
The only missing piece is wiring it up from the macOS app bundle.

## Proposed Architecture

Run a single long-lived `devdraw -s p9p-9term` process as the `NSApplication`.
All terminal windows live as `NSWindow`s inside this one process. Each `9term`
client connects to it via `$wsysid` instead of forking its own `devdraw`.

```
9term.app launch
  └─ devdraw -s p9p-9term   (NSApplication, single Dock icon, menu bar owner)
       ├─ NSWindow #1  ←  9term client (connected via $wsysid)
       ├─ NSWindow #2  ←  9term client (connected via $wsysid)
       └─ NSWindow #3  ←  9term client (connected via $wsysid)
```

`Cmd+\``, `Cmd+Tab`, Dock icon, menu bar — all work natively because there is
only one `NSApplication`. No `winreg` IPC, no policy toggling, no cross-process
activation hacks.

## How It Works End-to-End

### Startup

1. `9term.app/Contents/MacOS/9term` (the launcher script) starts `devdraw -s
   p9p-9term` as a background daemon. `devdraw` calls `gfx_main()`, which calls
   `gfx_started()`, which calls `listenproc` and then `[NSApp run]`. The app is
   now running with no windows yet.

2. The launcher script then execs the first `9term` client with
   `wsysid=p9p-9term/term1` in the environment. `9term` calls `initdraw()`,
   which calls `_displayconnect()`, which sees `$wsysid`, dials
   `$NAMESPACE/p9p-9term`, sends a `Tctxt` message with id `term1`, and gets
   back `Rctxt`. The server's `listenproc` has accepted the connection and
   spawned a `serveproc` for this client. When `9term` calls `initdraw()` with
   its label, the server calls `rpc_attach()` which dispatches to the main
   thread and creates an `NSWindow`.

### New Window

`newWindow:` in `mac-screen.m` no longer spawns a new `devdraw`. Instead:

1. Generate a unique client id (e.g. `term<n>`).
2. Set `wsysid=p9p-9term/<id>` in the child environment.
3. `posix_spawn` only the `9term` binary (not the launcher script). The new
   `9term` dials the already-running server, gets a new `NSWindow`.

### Window Close

When a `9term` client exits, its connection to the server closes. `serveproc`
sees EOF and calls `rpc_shutdown()` for that client, which closes and releases
the associated `NSWindow`. If it was the last window,
`applicationShouldTerminateAfterLastWindowClosed:` returns `YES` and the whole
app exits.

### App Quit

`applicationShouldTerminate:` sends `SIGTERM` to all live `9term` child PIDs
(tracked in a simple array), then returns `NSTerminateNow`. The server socket
closes, all `serveproc` threads see EOF and exit.

## Required Changes

### 1. `devdraw.c` — expose `-s` mode cleanly

The `-s` flag already exists. Verify `listenproc` correctly handles concurrent
clients and that `rpc_shutdown()` for one client does not call
`threadexitsall()` (currently it does when `c == client0`). Fix: only call
`threadexitsall()` when the last client disconnects, or not at all in server
mode (let `applicationShouldTerminateAfterLastWindowClosed:` drive exit).

```c
// In serveproc, replace:
if(c == client0) {
    rpc_shutdown();
    threadexitsall(nil);
}
// With:
rpc_clientgone(c);  // new function: close NSWindow, free Client
```

### 2. `mac-screen.m` — server-mode startup and window lifecycle

**`gfx_started()`** — when in server mode (`srvname != nil`), do not create
`client0`'s window immediately. Start `listenproc` and enter `[NSApp run]`
with no initial window. Show a window only when the first client connects and
calls `rpc_attach()`.

**`rpc_attach()`** — already creates an `NSWindow` per client. No change
needed here, but ensure it works when called for the 2nd, 3rd… client
(currently it does — `DrawView -attach:` allocates a fresh `NSWindow` each
time).

**`rpc_clientgone(Client *c)`** — new function called when a client's
`serveproc` exits. Dispatches to the main thread, releases the `DrawView` and
closes the `NSWindow` for that client.

**`newWindow:`** — replace `posix_spawn` of the launcher script with
`posix_spawn` of the `9term` binary directly, passing `wsysid=p9p-9term/<id>`
in the environment. No `P9P_SECONDARY` needed.

**Remove entirely**: `winreg_*` server/client, `p9p_is_secondary`,
`activateprimary` IPC, `applicationWillBecomeActive` policy toggling,
`applicationDidResignActive` policy toggling, `winreg_focused_pid` redirect
logic. All of this becomes unnecessary.

**`applicationShouldTerminateAfterLastWindowClosed:`** — return `YES` (or
`client0 != nil` check can be replaced with a live-client count).

**`applicationShouldTerminate:`** — send `SIGTERM` to tracked child PIDs.

### 3. `9term.app/Contents/MacOS/9term` (launcher script)

Replace the current script with one that:

1. Starts `devdraw -s p9p-9term` in the background (or checks if it's already
   running and reuses it — useful for re-opening the app from the Dock).
2. Waits for the socket to appear (poll `$NAMESPACE/p9p-9term`).
3. Execs the first `9term` client with `wsysid=p9p-9term/term0`.

```sh
#!/bin/sh
PLAN9=/usr/local/plan9
export PLAN9
PATH=$PLAN9/bin:$PATH
DEVDRAW=$PLAN9/bin/devdraw
export DEVDRAW

SRVNAME=p9p-9term
SOCKPATH=$(namespace)/$SRVNAME

# Start the server if not already running.
if ! [ -S "$SOCKPATH" ]; then
    "$DEVDRAW" -D -s "$SRVNAME" &
    # Wait for socket to appear (up to 2s).
    for i in $(seq 1 40); do
        [ -S "$SOCKPATH" ] && break
        sleep 0.05
    done
fi

trap '' HUP
unset NOLIBTHREADDAEMONIZE
exec "$PLAN9/bin/9term" -wsysid "$SRVNAME/term0"
```

Note: `9term` does not currently accept a `-wsysid` flag — the `wsysid` must
be passed as an environment variable (`wsysid=p9p-9term/term0`). The script
should `export wsysid=...` instead.

### 4. `9term` binary — no changes needed

`9term` calls `initdraw()`, which calls `_displayconnect()`, which already
handles `$wsysid`. No changes to `9term.c` are required.

### 5. Cmd+` and window cycling

With a single `NSApplication`, `Cmd+\`` works natively via
`NSApplication`'s standard window cycling. No custom `cycleWindows:` needed.
The existing menu item can use the standard `@selector(selectNextKeyView:)` or
simply be removed in favour of the OS default.

## What Gets Deleted

The following code in `mac-screen.m` can be removed entirely once the
migration is complete:

- `winreg_*` types, globals, and functions (~200 lines)
- `p9p_is_secondary` flag and all checks
- `P9PApplication -sendEvent:` override (the `activateprimary` hack)
- `applicationWillBecomeActive:` (policy promotion)
- `applicationDidResignActive:` (policy demotion)
- `applicationDidBecomeActive:` redirect logic
- `newWindow:` posix_spawn of launcher script (replaced with direct 9term spawn)
- `winreg_focused_pid`, `winreg_redirect_done`, `winreg_skip_redirect`

Estimated net reduction: ~350 lines of complex, fragile IPC code.

## Migration Path

The change can be done incrementally without breaking the existing behaviour:

1. **Phase 1**: Fix `serveproc`/`rpc_shutdown` so server mode doesn't
   `threadexitsall` on first client disconnect. Add `rpc_clientgone()`.
   Test with `DEVDRAW="devdraw -s foo" acme` manually.

2. **Phase 2**: Update `mac-screen.m` `gfx_started()` to not require
   `client0` in server mode. Update `newWindow:` to spawn `9term` with
   `$wsysid`. Test "New Window" with the server already running.

3. **Phase 3**: Update the launcher script to start `devdraw -s` and set
   `$wsysid` for the first client.

4. **Phase 4**: Delete all `winreg_*` and `p9p_is_secondary` code.

## Open Questions

- **Re-open from Dock**: If the user closes all windows but the app stays in
  the Dock (via `applicationShouldTerminateAfterLastWindowClosed: NO`), and
  then clicks the Dock icon, `applicationShouldHandleReopen:hasVisibleWindows:`
  should spawn a new `9term` client. This requires tracking whether the server
  socket is still live.

- **Multiple `9term.app` instances**: If the user launches `9term.app` a
  second time (e.g. double-clicks in Finder), macOS will activate the existing
  instance rather than launching a new one (standard single-instance app
  behaviour). This is the correct behaviour and requires no special handling.

- **`wsysid` uniqueness**: Each client needs a unique id within the server.
  A simple global counter in `newWindow:` is sufficient.

- **Window title / `Tlabel`**: Currently `rpc_setlabel` sets the `NSWindow`
  title. This continues to work per-client since each client has its own
  `DrawView` and `NSWindow`.

- **Retina / DPI**: Each `DrawView` already handles its own backing scale.
  No change needed.

- **`acme.app`**: The same approach applies. Acme already runs as a single
  process with one `devdraw`, so it would benefit less, but the launcher could
  be unified.
