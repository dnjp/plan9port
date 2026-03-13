# plan9port — extended distribution

This is a fork of [plan9port](https://github.com/9fans/plan9port) by Russ Cox,
which ports the Plan 9 userspace to Unix systems. This distribution layers
additional tooling, macOS integration, and editor enhancements on top of the
upstream codebase. This document describes everything that differs from
upstream so that experienced plan9port users can orient themselves quickly.

Refer to the standard man pages with `man <topic>` (or `9 man <topic>` outside
the plan9port environment) for authoritative reference on any tool.

---

## Installation

### Fresh macOS install

```sh
git clone <this repo>
cd plan9port
sudo ./SETUP
```

`SETUP` builds and installs everything under `/usr/local/plan9`, then:

1. Generates shell environment files at `$PLAN9/shell/` (see [Shell
   environment](#shell-environment) below).
2. Detects macOS and installs LaunchAgents for the background daemons
   (see [Background daemons](#background-daemons) below).

### Shell environment

After installation, add one line to your shell profile so that `PLAN9` and
`$PLAN9/bin` are always available:

| Shell | File | Line |
|-------|------|------|
| bash / zsh / sh | `~/.bash_profile` or `~/.zprofile` | `. /usr/local/plan9/shell/plan9.sh` |
| fish | `~/.config/fish/config.fish` | `source /usr/local/plan9/shell/plan9.fish` |
| rc | `~/.profile` | `. /usr/local/plan9/shell/plan9.rc` |

The sourced file sets `PLAN9`, adds `$PLAN9/bin` to `PATH` (idempotently), and
establishes default values for the font and dump variables described in the next
section. You can override any of these defaults after the source line.

#### Font variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `fixfont` | Monospace font for acme bodies, sam, 9term (`-F`/`-f`/`$font`) | `Menlo-Regular` 14pt + 28pt HiDPI |
| `varfont` | Proportional font for acme directory listings (`-f`) | `LucidaGrande` 13pt + 26pt HiDPI |
| `acmedump` | Acme dump file to restore on startup (`-l`) | `$HOME/acme.dump` |

Fonts are served by the `fontsrv` daemon via the `/mnt/font` path. Each value
is a comma-separated `normal,HiDPI` pair; acme selects the appropriate member
based on the display's pixel density. Both fonts are standard macOS system
fonts available on every macOS version.

These variable names match the conventions used by `startacme(1)` and
`start9term(1)` and the argument names in `acme(1)`.

---

## Application launchers

Upstream plan9port ships `.app` bundles that launch acme, sam, and 9term, and
separate helper scripts (`startacme`, `start9term`, `startsam`) that apply
fonts and start daemons. This fork consolidates everything into a single
wrapper script per tool.

### How it works

Each tool's binary is installed as `<tool>.bin`. The `<tool>` name in `$PLAN9/bin`
is now a shell script that:

1. Sources the user's login profile if `fixfont` is not already set — covering
   both the `.app` launch path and any other headless context (e.g. the
   plumber spawning acme when it is not running).
2. Applies `fixfont`, `varfont`, and `acmedump` defaults via `${var:=default}`.
3. Selects the native CPU architecture on Apple Silicon (`arch -arch arm64`).
4. Handles acme namespace collisions (a fresh `$NAMESPACE` when acme is
   already running).
5. Execs the binary with `argv[0]` set to the tool name, so the macOS menu
   bar, `pgrep`, and `skhd` all see `acme` / `sam` / `9term` rather than the
   full binary path.

The `.app` bundle launchers are now minimal: they set `INSIDE_P9P=true` and
exec the wrapper script. `INSIDE_P9P` signals to your interactive shell rc
that it is running inside a plan9port application (see
[Shell session inside a p9p app](#shell-session-inside-a-p9p-app) below).

The old `acme-start`, `acme-exec`, `start9term`, `startsam`, and `sam-exec`
scripts have been removed.

---

## Background daemons

Upstream plan9port starts `plumber`, `fontsrv`, and `ruler` from inside the
`Acme.app` launcher, meaning nothing works unless Acme is running. This fork
runs them as independent macOS LaunchAgents so they are always available.

The LaunchAgent plists and the helper that starts the daemons live in
`$PLAN9/mac/daemons/`. They are managed through the `mac/mkfile`:

```
mk daemons   # install LaunchAgent plists into ~/Library/LaunchAgents
mk load      # launchctl load (start) all agents
mk unload    # launchctl unload (stop) all agents
mk reload    # unload then load (restart after config changes)
mk status    # show agent state and PID
mk logs      # tail all daemon log files
```

`SETUP` runs `mk daemons && mk load` automatically on macOS.

Each daemon is started by `run-with-env.sh`, which:

- Sources `~/.profile` or `~/.bash_profile` to pick up `PLAN9` and `PATH`.
- Sets `NAMESPACE=/tmp/ns.$USER.:0` explicitly, matching the namespace used
  by interactive plan9port sessions.
- Kills any existing instance and removes stale 9p sockets before starting,
  so restarts are always clean.
- Detaches the daemon's `9pserve` child into a new process group (macOS
  lacks `setsid(1)`; a `(set +m; cmd &)` subshell is used instead) so that
  launchd reaping the wrapper does not kill the child.
- Sleeps in a loop to keep the LaunchAgent job alive while the daemon runs.

### Plumber and acme auto-start

The plumber's default rules (`plumb/basic`) are updated so that every "open
file in editor" rule starts acme automatically if it is not already running,
waits for its 9p port to appear, then delivers the message. This means
`plumb <file>` always opens the file in acme regardless of whether acme was
already running.

See `plumb(1)`, `plumber(4)`, `plumb(7)`.

---

## Shell session inside a p9p app

When your shell is running as a child of acme, sam, or 9term, certain
interactive shell features are inappropriate (ANSI completions, syntax
highlighting, `$TERM=xterm`, etc.) and certain plan9port behaviours should
be active (`TERM=dumb`, `PAGER=nobs`, `awd` for window title, and so on).

The variable `INSIDE_P9P` is set to `true` by every `.app` launcher. Source
`$PLAN9/shell/p9p-session.sh` when this variable is set to get the right
behaviour automatically:

```sh
# in ~/.bashrc or ~/.zshrc
if [ -n "$INSIDE_P9P" ]; then
    . "$PLAN9/shell/p9p-session.sh"
else
    # normal interactive setup: completions, plugins, etc.
fi
```

`p9p-session.sh` does the following:

- Calls `awd` to set the acme window title to the current directory.
- Overrides `cd` so every directory change calls `awd`.
- Sets `TERM=dumb` (acme/sam are not ANSI-compatible terminals).
- Sets `PAGER=nobs` and `MANPAGER=nobs` (strips backspace-bold sequences).
- Disables readline `emacs` and `vi` editing modes.
- Enables `set -a` (allexport), matching rc shell convention.

A fish variant is provided at `$PLAN9/shell/p9p-session.fish`.

`INSIDE_ACME`, `INSIDE_SAM`, and `INSIDE_9TERM` are also exported individually
by their respective launchers for scripts that need to distinguish between them.

---

## Ruler — rule-based configuration

`ruler` is a new 9p file server that applies per-file configuration to editor
windows. It is conceptually similar to the plumber: clients query it with a
file path and context, and it returns a set of key/value directives from a
user-defined rule file.

Acme queries ruler automatically when a new window is opened, applying
settings such as tab width, tab expansion, indentation, and comment format
on a per-file-type basis — without any buffer-local configuration syntax.

### Running ruler

`ruler` is started as a LaunchAgent (see [Background daemons](#background-daemons)).
It can also be started manually:

```sh
ruler
```

By default it loads `$HOME/lib/rules` if that file exists, falling back to
`$PLAN9/rule/initial.rules`.

### Querying ruler

```
rule [-q query] [-c client] [-e event] [-i id]
```

`rule` is the command-line client for ruler. Without flags it reads a raw
request from stdin. With flags it composes the request from its arguments.
If `-q` is omitted but other flags are given, the query is read from stdin.
See `rule(1)` and `ruler(7)` for request format details.

### Writing rules

Rules live in `$HOME/lib/rules` (or `$PLAN9/rule/initial.rules` for
system-wide defaults). The format is similar to plumbing rules: each rule
set contains pattern lines and `rule set key value` directive lines, separated
by blank lines. Rules are matched in order; the first match wins.

```
# Go files: use gofmt tab style
client is acme
event is open
query matches '*.go'
rule set tabstop 8
rule set tabexpand off
rule set comfmt '// %s'

include initial.rules
```

The `include` directive works like a C `#include`, pulling in rules from
`$PLAN9/rule/` or `$HOME/lib/ruler/`. Override upstream defaults by placing
your rules *before* the `include` line.

See `ruler(4)` for the daemon reference, `ruler(7)` for the full rule and
request format, and `rule(1)` for the command-line client.

---

## Acme enhancements

Several features have been added to acme beyond what upstream provides. All
are documented in `acme(1)`.

### Keyboard shortcuts

| Key (Mac) | Action |
|-----------|--------|
| `Command-S` | Save the current file (equivalent to `Put` with no argument) |
| `Command-/` | Toggle comment on selected lines using the window's `Comfmt` |
| `Command-+` / `Command--` | Increase / decrease font size globally |
| `Tab` (with selection) | Indent selected lines by one level |
| `Shift-Tab` (with selection) | Unindent selected lines by one level |
| `A+` / `A-` | Indent / unindent selected lines (alternate binding) |
| `Command-Left` / `Command-Right` | Move to start / end of current line |

### Commands

| Command | Description |
|---------|-------------|
| `Com` / `Uncomment` | Toggle comment on selected lines |
| `Comfmt <format>` | Set the comment format for this window (must contain `%s`) |
| `Tabexpand` | Toggle tab expansion (insert spaces instead of tabs) |
| `Indent` | Toggle autoindent mode |
| `F+` / `F-` | Increase / decrease font size |

`F+`/`F-` behaviour depends on where the command is executed:

- In the **row tag**: resize both default body fonts and tag fonts globally,
  updating all windows.
- In a **column tag**: resize the body font of every window in that column.
- In a **window tag or body**: resize only that window's body font.

### Ruler integration

Acme queries `ruler(4)` automatically when a window is opened (`event=open,
client=acme`). The following directive keys are applied if present in the
response:

| Key | Effect |
|-----|--------|
| `tabstop` | Tab width in character units |
| `tabexpand` | `on` or `off` — insert spaces instead of tabs |
| `indent` | `on` or `off` — autoindent mode |
| `comfmt` | Comment format string for `Com`/`Uncomment` |

---

## New tools

### `editinacme`

```
editinacme <file>
```

Opens a file in acme via the plumber and blocks until the acme window for that
file is closed (deleted). Designed for use as `$EDITOR` when acme is running:

```sh
export EDITOR=editinacme
export VISUAL=$EDITOR
export GIT_EDITOR=$EDITOR
```

`editinacme` resolves the file to an absolute path, plumbs it to acme, then
watches `acme/log` for a `del` event on that path before exiting. This makes
it suitable for `git commit`, `crontab -e`, and any other tool that opens
`$EDITOR` and waits for it to exit.

See `editinacme(1)`.

### `acmeaddr`

```
acmeaddr [winid]
```

Prints the current selection address of an acme window as two rune offsets
(`q0 q1`), suitable for passing to other tools. Uses `$winid` if no argument
is given, so it works naturally when invoked from an acme window tag with
`Run`.

See `acmeaddr(1)`.

### `gitlink`

```
gitlink [file [line1 [line2]]]
```

Prints the remote Git hosting URL (GitHub, GitLab, Bitbucket) for the current
acme buffer, optionally with a line or line-range fragment. When run from an
acme window tag with no arguments, it uses `$winid` and the current selection
to construct the URL. Useful for sharing precise file locations from within
the editor.

### `rule`

```
rule [-q query] [-c client] [-e event] [-i id]
```

Command-line client for the `ruler(4)` daemon. See [Ruler](#ruler--rule-based-configuration) above and `rule(1)`.

---

## File layout

Notable additions relative to upstream:

```
$PLAN9/
  bin/
    editinacme      open a file in acme and wait for close
    acmeaddr        print current acme selection address
    gitlink         print Git hosting URL for current acme buffer
    rule            ruler command-line client
  shell/
    plan9.sh        POSIX sh/bash/zsh environment setup (source from profile)
    plan9.fish      fish environment setup
    plan9.rc        rc environment setup
    p9p-session.sh  interactive shell setup inside a p9p app
    p9p-session.fish fish variant of p9p-session
  mac/
    daemons/        LaunchAgent plists and support scripts for background daemons
    Acme.app/   9term.app/   Sam.app/   Plumb.app/
  rule/
    initial.rules   built-in ruler rules for common file types
    basic           language-specific ruler rule library
  plumb/
    basic           plumbing rules (updated to auto-start acme when needed)
  src/cmd/
    rule/           source for both ruler(4) daemon and rule(1) client
    acmeaddr.c      source for acmeaddr(1)
```

---

## Man pages

| Page | Description |
|------|-------------|
| `man 1 acme` | Acme editor — includes all keyboard/command additions |
| `man 1 rule` | rule(1) — ruler command-line client |
| `man 4 ruler` | ruler(4) — ruler daemon reference |
| `man 7 ruler` | ruler(7) — ruler request/response/rulerfile format |
| `man 1 editinacme` | editinacme(1) |
| `man 1 acmeaddr` | acmeaddr(1) |
| `man 1 plumb` | plumb(1) — sending plumbing messages |
| `man 4 plumber` | plumber(4) — plumber daemon |
| `man 7 plumb` | plumb(7) — plumbing rule format |
| `man 4 acme` | acme(4) — acme file server protocol |
| `man 1 fontsrv` | fontsrv(1) — font server |
| `man 1 9p` | 9p(1) — 9P file operations |
