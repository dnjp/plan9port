# Editor Rules Service — Specification

This document specifies a plumber-like external service that provides filetype-specific configuration (comment style, tab width, indentation, etc.) to editors such as Acme and Sam. Editors query the service when opening or saving files; the service matches the file path against a rules file and returns a list of directives. The implementation lives in plan9port, is written in Plan 9 C, and is used by Acme and Sam (with protocol support embedded in each editor).

---

## 1. Purpose and context

### 1.1 Goal

- Apply **filetype-specific settings** automatically when a file is opened (or saved) in an editor: comment format (`comfmt`), tab expansion (`tabexpand`), tab width (`tabstop`), autoindent (`indent`), and in the future optional actions such as formatters on save.
- Support **multiple editor instances**: any number of Acme or Sam processes may be running; each queries the service when it opens a file. No registration or discovery of editors by the service.
- **Single source of truth**: one rules file per editor type (e.g. `acmerules`, `samrules`), plumber-like pattern matching, so configuration is file-based and external to the editors.

### 1.2 Design principles (Plan 9–style)

- **External service**: policy lives in a daemon and rules files, not inside the editors. Editors are extended only to “ask and apply.”
- **Plumber-like**: one daemon, one well-known address, request/response, pattern → directives. No need for the service to know where editors are; the client that asked receives the response and applies it.
- **Convention**: rules files in `$home/lib`, service address via `$rules` (or default). Short, lowercase names consistent with other Plan 9 services.

### 1.3 Scope of this spec

- **In plan9port**: the service implementation (daemon), the protocol, and the editor-side support (Acme, and later Sam) all live in the plan9port repository and are implemented in Plan 9 C.
- **Service name**: see §5 (name brainstorming). The protocol and attribute that identifies the editor are both expressed with the term **editor** (e.g. `editor=acme`), not “app.”

---

## 2. Architecture

### 2.1 Components

| Component | Location | Responsibility |
|-----------|----------|----------------|
| **Editor rules daemon** | plan9port (e.g. `src/cmd/rules` or `src/cmd/ruled`) | 9P server; loads rules files; on request, matches path and returns directives. |
| **Rules files** | User: `$home/lib/acmerules`, `$home/lib/samrules` (or `$acmerules`, `$samrules`) | Pattern → directive list; one file per editor type. |
| **Acme** | plan9port `src/cmd/acme` | On window file load: connect to service, send request, read response, apply directives. |
| **Sam** | plan9port `src/cmd/sam` | (Later) Same: on file open, query service, apply directives. |

### 2.2 Flow

1. User opens a file in Acme (e.g. via Get, Load, New, or plumb). Acme determines the window’s file path.
2. Acme connects to the editor rules service at a well-known address (e.g. `$rules` or default `$home/lib/rules`).
3. Acme writes one **request** (attributes: `path`, `editor`, `event`, optionally `instance`, `window`).
4. Service matches `path` and `editor` against the appropriate rules file (e.g. `acmerules` for `editor=acme`), first match wins.
5. Service writes back one **response**: newline-separated lines of the form `key=value` (directives).
6. Acme parses the response and applies each known directive (Comfmt, Tab, Tabexpand, Indent, etc.). Unknown keys are ignored.
7. Connection is closed. No state is kept in the service about which Acme or window asked.

Multiple Acmes (or Sams) all use the same service; each request is independent.

---

## 3. Protocol

### 3.1 Transport

- **Plan 9**: 9P server, e.g. posted under `srv` or bound at a conventional path (e.g. `$home/lib/rules`).
- **Unix / plan9port**: 9P over a Unix socket or TCP, at a well-known path (e.g. `$home/lib/rules` or `$rules`). Same 9P file tree as on Plan 9.

### 3.2 9P file tree

The service exposes at least:

| File | Mode | Semantics |
|------|------|-----------|
| **`query`** | read/write | One request per write; one response per read. Client opens, writes request body, reads response body, closes. Each open/fid is one request/response pair. |
| **`reload`** | write | Writing causes the daemon to reload all rules files from disk (e.g. after editing `acmerules`). Optional. |

Optional: read-only file(s) or symlinks showing which rules files are loaded (e.g. for debugging).

### 3.3 Request message (client → service)

Written to `query`. Attribute-value format, one attribute per line (or same line, space-separated; define one and stick to it). Suggested:

```
path=/absolute/or/relative/path/to/file.go
editor=acme
event=open
instance=12345
window=7
```

- **path** (required): File path to match against rules. Should be the path used to open the file (absolute preferred; if relative, server may resolve or match as-is).
- **editor** (required): Which editor is asking. Selects the rules file: `acme` → `acmerules`, `sam` → `samrules`. Values: `acme`, `sam` (extensible).
- **event** (optional): `open` or `save`. Enables different rules or directives later (e.g. “on save run formatter”). Default `open`.
- **instance**, **window** (optional): Identifiers for the editor instance and window. Not required for matching; useful for logging or future push-based features.

### 3.4 Response message (service → client)

Read from `query` after the request. Body is zero or more lines:

```
key=value
key=value
```

- No match or empty rules: empty response (zero bytes or no lines).
- Each line is one directive. Editor parses `key=value` and applies known keys; ignores unknown keys.
- No continuation or quoting; values must not contain newlines. Optional: define escaping if needed later.

### 3.5 Directive names (editor-specific)

**Acme** (from `acmerules`):

| key | Meaning | Acme action |
|-----|---------|-------------|
| `comfmt` | Comment format string (e.g. `// %s`) | Comfmt command |
| `tabstop` | Tab width in units of zero character | Tab command |
| `tabexpand` | `on` / `off` | Tabexpand command |
| `indent` | `on` / `off` | Indent command |

Future (e.g. save hook): `puthook=program` to run a formatter on Put.

**Sam** (from `samrules`): To be defined when Sam support is added (e.g. tab width, comment style if Sam has equivalents).

---

## 4. Rules file format

### 4.1 Location and naming

- **Acme**: `$home/lib/acmerules` or `$acmerules` (if set).
- **Sam**: `$home/lib/samrules` or `$samrules`.
- Daemon loads these at startup and on `reload` (if implemented).

### 4.2 Syntax (plumber-like)

- Lines are either **rules** or **comments**.
- Comment: `#` to end of line.
- Rule: one or more **patterns**, then whitespace, then one or more **directives** (key=value), separated by spaces or tabs.
- **First match wins**: patterns are tried in order; the first rule whose pattern matches the request path is used; its directives are returned.

Pattern types (to be implemented in daemon):

- **Suffix**: `*.c`, `*.go` — path ends with the given suffix.
- **Prefix**: `/lib/`, `$home/src/` — path starts with the given prefix.
- **Exact** (optional): path equals a string.
- **Regex** (optional): path matches a regex. Use with care for performance.

Directives: same keys as in §3.5 (e.g. `comfmt=// %s`, `tabexpand=on`, `tabstop=8`).

Example `acmerules`:

```
# C
*.c *.h              comfmt=/* %s */ tabexpand=on tabstop=8
# Go
*.go                 comfmt=// %s tabexpand=on tabstop=4
# Python
*.py                 comfmt=# %s tabexpand=on tabstop=4 indent=on
# Default for /lib
/lib/                tabstop=4
```

### 4.3 Matching algorithm

1. Normalize path if needed (e.g. resolve `$home` in prefix patterns).
2. For each rule in order: if any pattern in the rule matches the request path, return that rule’s directives and stop.
3. If no rule matches, return empty response.

---

## 5. Service name

The service and protocol should have a short name that fits with Plan 9 (e.g. plumber, auth, srv, 9fs). The protocol and request attribute use **editor** (e.g. `editor=acme`), not “app.”

**Name ideas:**

| Name | Notes |
|------|--------|
| **rules** | Clear, short. Service could be `srv/rules`; binary `rules` or daemon `rulesd`. |
| **ruled** | “Rules daemon,” like plumber is the plumb daemon. |
| **edrules** | “Editor rules”; compact. |
| **rul** | Very short; might be too terse. |
| **editrules** | Explicit but longer. |

**Recommendation:** **rules** as the service name (and 9P tree name). Binary can be `rules` (the program that runs the server). Rules files stay `acmerules` and `samrules` (editor-specific). Protocol can be referred to as “editor rules protocol” or “rules protocol.”

---

## 6. Implementation requirements (plan9port)

### 6.1 Daemon (Plan 9 C, in plan9port)

- **Location**: e.g. `src/cmd/rules/` (or `src/cmd/ruled/`).
- **Behavior**: 
  - Listen on well-known address (see §6.3).
  - Expose 9P tree with `query` (and optionally `reload`).
  - On write to `query`: parse request (path, editor, event, …), select rules file by `editor`, match path, format directive lines, store response for that fid.
  - On read from `query`: return the response for that fid.
  - Load rules from `$home/lib/acmerules`, `$home/lib/samrules` (or env overrides); reload on SIGHUP or write to `reload`.
- **Pattern matching**: implement at least suffix and prefix; regex optional.
- **Concurrency**: multiple clients (multiple Acmes/Sams) may connect; handle concurrent requests (e.g. one fid per request, or internal queueing).

### 6.2 Acme changes (Plan 9 C, in plan9port)

- **Trigger**: When a window’s body has just been associated with a file (e.g. after loading file at startup, Get, Load, or plumb open). Single call site or small set of call sites that “file F is now the content of this window.”
- **Steps**:
  1. Resolve full path (or canonical path) for the window’s file.
  2. If no path or not a regular file (e.g. directory), skip.
  3. Open connection to editor rules service (see §6.3). If `$rules` unset or connection fails, skip silently.
  4. Open `query`, write request: `path=...`, `editor=acme`, `event=open` (and optionally instance/window if useful).
  5. Read full response.
  6. Close connection.
  7. Parse response line by line; for each known key, apply the corresponding Acme setting (Comfmt, Tab, Tabexpand, Indent). Ignore unknown keys.
- **No UI change**: no new menus or commands; feature is automatic and best-effort.

### 6.3 Well-known address

- **Environment**: `$rules` — if set, use it as the service address (path or socket).
- **Default**: e.g. `$home/lib/rules` (directory or socket path). On plan9port, if 9P is over a socket, convention could be `$home/lib/rules/socket` or a single socket at `$home/lib/rules`.
- Document in man page and in Acme/Sam docs.

### 6.4 Sam (later)

- Same pattern: when a file is opened (or buffer created with a path), Sam sends `path=...`, `editor=sam`, reads response, applies Sam-specific directives. Implementation in `src/cmd/sam`, same protocol.

### 6.5 Failure and absence

- If the service is not running or not configured: editors must not block and must not show errors for “rules unavailable.” Simply skip applying rules (same as “no match”).
- If a directive is unknown or value invalid: ignore that line and continue.

---

## 7. Future extensions (out of scope for initial spec)

- **event=save**: Different rules or directives on save; e.g. “run formatter before writing.”
- **puthook / save hook**: Directive like `puthook=goimports`; editor runs the program on the buffer and replaces with stdout before Put.
- **More editors**: Same protocol, new `editor` value and corresponding rules file (e.g. `samrules`).
- **Daemon pushes**: Optional “reply-to” or callback so the daemon could push updates; would require editors to expose a receive endpoint. Not required for the initial design.

---

## 8. Summary

| Item | Choice |
|------|--------|
| Service name | **rules** (recommended); alternatives: ruled, edrules. |
| Protocol / attribute | **editor** (e.g. `editor=acme`, `editor=sam`). |
| Rules files | `$home/lib/acmerules`, `$home/lib/samrules` (or env). |
| Transport | 9P server; well-known address `$rules` or default `$home/lib/rules`. |
| Request | Write to `query`: path, editor, event, optional instance/window. |
| Response | Read from `query`: newline-separated `key=value` lines. |
| Implementation | Plan 9 C, all inside plan9port: daemon + Acme (and later Sam) client logic. |

This spec is intended to be self-contained so that implementation can be resumed in a future session without relying on the original conversation.
