# Ruler — Specification

This document specifies **ruler**: a plumber-like service that returns **configuration** based on an **event** and a **rulerfile** (a set of rules). The service is agnostic to what that configuration is for. Ruler only interprets four request attributes: **query**, **client**, **event**, and **id**; it matches them against the rulerfile and returns whatever directives the matching rule specifies. Ruler does not define or interpret the response content—it is derived entirely from the rulerfile. The **first use** is configuring Acme and Sam when opening or saving files (for them, the query is a file path); implementation lives in plan9port, in Plan 9 C.

---

## 1. Purpose and context

### 1.1 Goal

- **Return configuration from rules**: clients send a request with **query**, **client**, **event**, and **id**; ruler matches against a single **rulerfile** and returns the directives from the first matching rule. The response content is whatever the rulerfile specifies; ruler does not interpret it. What clients do with those directives is up to them.
- **Event- and rule-driven**: rules match on query, client, event; first match wins. No registration or discovery—the client that asked gets the response.
- **Single source of truth**: one rulerfile; pattern matching (query, optional client scope). Configuration is file-based and external to the clients.

### 1.2 Design principles (Plan 9–style)

- **External service**: policy lives in the daemon and the rulerfile, not inside the clients. Clients “ask and apply.”
- **Plumber-like**: one daemon, one well-known address, request/response, pattern → directives.
- **Convention**: rulerfile (config) at **`$rulerfile`** (default `$home/lib/rulerfile` or `~/lib/rulerfile`). Ruler service is posted as **ruler** (srv/ruler). Short, lowercase names.

### 1.3 Scope of this spec

- **In plan9port**: the service (binary **ruler**), the protocol, and the first client support (Acme, then Sam) live in the plan9port repository, Plan 9 C.
- **First use**: editors (Acme, Sam) query ruler on file open/save; for them the **query** is the file path. They apply the returned directives (comfmt, tabstop, etc.). Other client types and query semantics can be added later.

---

## 2. Architecture

### 2.1 Components

| Component | Location | Responsibility |
|-----------|----------|----------------|
| **Ruler** | plan9port (e.g. `src/cmd/ruler`) | 9P server posted as **ruler** (srv/ruler); reads rulerfile from **`$rulerfile`** (config path only). On request to `query`, matches **query**, **client**, **event**; returns directives from the matching rule. Exposes **`rules`** for reload (e.g. `9p write ruler/rules < $HOME/lib/rulerfile`). |
| **Rulerfile** | User: file at **`$rulerfile`** (default `$home/lib/rulerfile` or `~/lib/rulerfile`) | Config file; pattern → directive list; optional `client=` and `event=` scope per rule. Not a connection point. |
| **Acme** | plan9port `src/cmd/acme` | (First client.) On window file load: connect to ruler, send request, read response, apply directives. |
| **Sam** | plan9port `src/cmd/sam` | (Later) Same: on file open, query ruler, apply directives. |

### 2.2 Flow

1. (Example: Acme.) User opens a file; Acme determines the window’s file path and window id.
2. Acme connects to the **ruler service** (e.g. `srv/ruler` in the namespace) and opens `query`.
3. Acme writes one **request** (attributes: `query`, `client`, `event`, `id`). For Acme, query is the file path, client is `acme`, event is `open`, id is the window id.
4. Ruler matches **query**, **client**, and **event** against the rulerfile; first matching rule wins; ruler returns that rule’s directives verbatim (ruler does not interpret them).
5. Ruler writes back one **response**: the directive lines from the rulerfile (newline-separated `key=value`).
6. Acme parses the response and applies each known directive (Comfmt, Tab, Tabexpand, Indent, etc.). Unknown keys are ignored.
7. Connection is closed. No state is kept in ruler about who asked.
8. **Only then** does Acme publish the corresponding event to the event log (e.g. `get` for a loaded file). So ruler runs first; event-log readers (e.g. scripts that extend Acme) see the window only after ruler has been applied. Ruler thus has precedence; clients that read the event log are free to override or extend what ruler set.

Multiple clients use the same ruler; each request is independent.

---

## 3. Protocol

### 3.1 Transport

- **Plan 9 / Unix / plan9port**: The ruler daemon posts itself as the service **ruler** (e.g. via `post9pservice`), so it appears as **`srv/ruler`** in the namespace. Clients connect to that service (same pattern as the plumber at `srv/plumb`). The daemon reads its config from the file at **`$rulerfile`** (default `$home/lib/rulerfile`); that path is not a connection endpoint.

### 3.2 9P file tree

The service exposes at least:

| File | Mode | Semantics |
|------|------|-----------|
| **`query`** | read/write | **One request/response pair per fid.** The client opens (obtaining a fid), writes the request body (once), reads the response body (once), then closes. Multiple requests require multiple opens. Read before write returns empty. |
| **`rules`** | read/write | Read: return the rules as they are configured and parsed (same as plumber; see plumber docs and e.g. `printrules`). Write: reload rules from the written content (e.g. `9p write ruler/rules < $HOME/lib/rulerfile` to update the rules). |

Optional: SIGHUP also triggers reload from the file at `$rulerfile`.

### 3.3 Request message (client → ruler)

Written to the 9P file `query`. Attribute-value format, **one attribute per line** (newline-separated), for consistent parsing and to allow values that contain spaces.

The **only attributes ruler must know about** are; **all four are required** (if any is missing, ruler may treat the request as invalid and return an empty response):

| Attribute | Meaning |
|-----------|--------|
| **query** | The subject to match against rules. For text editors (first use), this is the file path (see below). Ruler does not interpret the value; it is matched as a string against rulerfile patterns. |
| **client** | Client identifier (e.g. `acme`, `sam`). Used when matching rules that include a client scope. |
| **event** | Event name (e.g. `open`, `save`). Used when matching rules that include an event scope (§4.2). The ruler can be called in any context; event identifies what is occurring. |
| **id** | Identity of the calling component (e.g. Acme window id). Used for logging or future use; not used for matching. |

When the client is Acme (or Sam), the **query** is expected to be the **absolute** path of the file (ruler does not normalize it). For other clients, the query may be something else entirely.

Example (Acme opening a file):

```
query=/absolute/path/to/file.go
client=acme
event=open
id=7
```

Ruler ignores any other attributes in the request. It does not define or interpret the response body—that is determined entirely by the rulerfile.

### 3.4 Response message (ruler → client)

Read from the 9P file `query` after the request. Body is zero or more lines of the form `key=value`, **exactly as written in the rulerfile** for the matching rule. Ruler does not interpret or validate the response content—it simply returns the directive lines from the rulerfile.

- No match or empty rules: empty response (zero bytes or no lines).
- Each line is one directive. The client interprets and applies directives; ruler is agnostic to their meaning.
- No continuation or quoting; values must not contain newlines. The **first** `=` on a line separates key from value; the rest of the line is the value, so values may contain additional `=` characters. Optional: define escaping if needed later.

### 3.5 Directive names (first use: Acme / Sam)

All directives come from the rulerfile. Each client applies only the keys it understands; unknown keys are ignored.

**Acme** (first client) understands:

| key | Meaning | Acme action |
|-----|---------|-------------|
| `comfmt` | Comment format string (e.g. `// %s`) | Comfmt command |
| `tabstop` | Tab width in character units | Tab command |
| `tabexpand` | `on` / `off` | Tabexpand command |
| `indent` | `on` / `off` | Indent command |

Future (e.g. save hook): `puthook=program` to run a formatter on Put.

**Sam** (when supported): e.g. `tabstop`, and comment style if Sam has an equivalent. Exact keys to be defined with Sam support.

---

## 4. Rulerfile format

### 4.1 Location and naming

- **Single file**: path given by **`$rulerfile`** (default `$home/lib/rulerfile` or `~/lib/rulerfile`).
- Ruler loads it at startup and when a client writes to **ruler/rules** or on SIGHUP.

### 4.2 Syntax (plumber-like)

- Lines are either **rules**, **comments**, or **include** directives.
- Comment: `#` to end of line.
- **Include**: A line of the form `include` *file* substitutes the contents of *file* for that line, as in C `#include`. The file name is not quoted. When ruler reads the main rulerfile (or an included file), it processes lines in order; when it hits `include` *file*, it pushes *file* onto an input stack and reads from it. When that file is exhausted, parsing continues after the include line. Include depth is limited (e.g. 10) to prevent infinite recursion and bad include graphs.
- **Include search path**: Same as the plumber (see plumb(7) and `src/cmd/plumb/rules.c`). If *file* is not absolute and not `./` or `../`, ruler looks first in the directory containing the current file (when loading from a file path), then in a conventional library directory. When rules are supplied by writing to **ruler/rules** (no disk file), ruler uses the same convention as the plumber for relative includes (e.g. plumber’s working directory then `/sys/lib/plumb` or plan9port equivalent; ruler: e.g. `$home/lib/ruler` or `#9/ruler/`).
- Rule: one or more **patterns** (matching the request **query**; optionally **client=** and **event=** for scope), then whitespace, then one or more **directives** (key=value), separated by spaces or tabs.
- **First match wins**: rules are tried in order (including rules from included files, in the order they appear after expansion). The first rule for which (a) a pattern matches the request **query**, (b) if the rule includes `client=acme` or `client=sam`, that value equals the request’s **client**, and (c) if the rule includes `event=open` or `event=save`, that value equals the request’s **event**, is used; its directives are returned verbatim by ruler. Ruler does not interpret the directives—they are opaque.

Pattern types (to be implemented in ruler):

- **Suffix**: `*.c`, `*.go` — query string ends with the given suffix (e.g. a file path for editors).
- **Prefix**: `/lib/`, `$home/src/` — query starts with the given prefix.
- **Exact** (optional): query equals a string.
- **Regex** (optional): query matches a regex. Use with care for performance.
- **Client** (optional): `client=acme` or `client=sam` — rule applies only when the request’s **client** matches. If omitted, the rule applies to all clients for that query.
- **Event** (optional): `event=open` or `event=save` (etc.) — rule applies only when the request’s **event** matches. If omitted, the rule applies to all events for that query.

Directives: key=value. The client interprets them; ruler just returns the lines from the rulerfile (e.g. §3.5 for what Acme/Sam do with them).

Example **rulerfile** (first use: Acme and Sam; query is file path):

```
include languages
# C — shared
*.c *.h              comfmt=/* %s */ tabexpand=on tabstop=8
# Go — Sam-specific rule first (first match wins), then Acme/shared
*.go                 client=sam tabstop=4
*.go                 comfmt=// %s tabexpand=on tabstop=4
# Python — shared
*.py                 comfmt=# %s tabexpand=on tabstop=4 indent=on
# Default for /lib
/lib/                tabstop=4
# Optional: event scope (e.g. different rules on save)
*.go                 event=save tabstop=4
```

### 4.3 Matching algorithm

1. When matching, prefix patterns may reference `$home`; ruler resolves that for the pattern. The request **query** is matched as given (ruler does not normalize it; for Acme/Sam the client is expected to send an absolute path).
2. For each rule in order: if (a) a pattern matches the request **query**, (b) the rule has no `client=` or its `client=` equals the request’s **client**, and (c) the rule has no `event=` or its `event=` equals the request’s **event**, return that rule’s directive lines and stop. Ruler does not interpret the directive content.
3. If no rule matches, return empty response.

---

## 5. Naming

- **Service**: **ruler**. Binary and 9P tree name. Agnostic to editing; it applies rules and returns configuration for an event and request. First use happens to be editors (Acme, Sam).
- **Config file**: **rulerfile**. Path from **`$rulerfile`** (default `$home/lib/rulerfile` or `~/lib/rulerfile`). **Not** the connection point—clients connect to the service **ruler** (srv/ruler).

---

## 6. Implementation requirements (plan9port)

### 6.0 Shared header and code layout

**Shared header (critical for compatibility)**  
Plan 9 services that use ruler (Acme, Sam, any future client) must share a single header that defines the request protocol: attribute names, optional struct(s) for building requests, and the response format contract. That way changes to ruler (e.g. new optional attributes) can be made without breaking clients, and clients use the same names and layout as the daemon.

- **Header**: **`include/ruler.h`** (in the plan9port tree; same convention as `include/plumb.h`). Both the ruler daemon and client code (e.g. Acme, Sam) `#include <ruler.h>`.
- **Contents**: At minimum, the canonical attribute names for the request (`query`, `client`, `event`, `id`)—**all four required**—and the wire format (one attribute per line, newline-separated; response is newline-separated `key=value` lines; first `=` on a line separates key from value). Optionally, a `Rulerreq` struct and helpers to build the request body or parse the response.

**Ruler daemon — code layout**  
Under **`src/cmd/ruler/`** (mirrors other Plan 9 cmds: main entry in a file named after the command):

| File | Responsibility |
|------|----------------|
| **ruler.c** | Main entry point; read rulerfile from `$rulerfile` at startup; create 9P listener and post service as **ruler** (e.g. `post9pservice` so it appears as `srv/ruler`); start 9P server. |
| **9p.c** | 9P file tree: expose **`query`** (read/write for request/response) and **`rules`** (read/write). On write to `query`, parse request (using the same attribute names as in the shared ruler header), call into match, store response for fid; on read, return it. **`rules`**: read returns all the rules (like plumber); write reloads rules from the written content (e.g. `9p write ruler/rules < $HOME/lib/rulerfile`). |
| **rules.c** | Load rulerfile (from file path or from written content when `rules` is written); handle `include` (input stack, search path, max depth); parse rules and patterns into an in-memory structure. Similar structure to `src/cmd/plumb/rules.c`. |
| **match.c** | Given a request (query string, client, event), match against the loaded rules; return the directive lines for the first matching rule (or empty). Pattern types: suffix, prefix, optional exact/regex; optional client= and event= scope. |
| **ruler.h** (internal) | Internal header for the daemon (rule structs, pattern types, declarations for rules.c / match.c). Distinct from the shared public header **`include/ruler.h`**. |
| **mkfile** | Build rules; link with required libs (e.g. thread, 9p). |

The daemon does not interpret directive content; it stores and returns the lines from the rulerfile. **`$rulerfile` is not a connection point**—it is the path to the config file the daemon reads at startup. Clients connect to the **ruler service** (see §6.3).

**Acme integration — where and in what file**  
- **Client logic**: **`src/cmd/acme/ruler.c`**. Connect to the **ruler service** (e.g. open `ruler/query` via the namespace, like `plumbopenfid` for plumber—see §6.3). Build the request (query = file path, client = `acme`, event = `open`, id = window id) using the shared ruler header, write to the 9P `query` file, read response, parse directive lines, apply known keys (Comfmt, Tab, Tabexpand, Indent) to the window. Export a single entry point, e.g. `void rulerapply(Window *w)`. If there is no path or ruler is unavailable, do nothing. No UI; best-effort and silent on failure.
- **Call sites**: Insert a call to this entry point **immediately before** each place Acme currently calls `xfidlog(w, op)` when the window has just been associated with a file. So: in **`exec.c`**, before `xfidlog(w, "get")` (after a successful Get), before `xfidlog(w, "new")` when the new window has a file, and before `xfidlog(nw, "zerox")`; in **`look.c`**, **`rows.c`**, **`util.c`**, **`acme.c`** (and any other file that calls `xfidlog(w, "new")` for a window with a file), call the ruler client before `xfidlog(w, "new")`. That preserves the required ordering: ruler runs first, then the event is published. Do **not** add ruler calls before `xfidlog(w, "del")` or `xfidlog(t->w, "focus")` (no file-open semantics). Acme’s **`dat.h`** (or equivalent) should declare the ruler client entry point so other modules can call it; the implementation stays in **`ruler.c`**.

**Sam (later)**  
Same idea: a **`ruler.c`** (or equivalent) under `src/cmd/sam/` that implements the ruler client for Sam, using `include/ruler.h`, and call it at the appropriate “file opened” site(s) before any event or log that corresponds to “window has file.”

---

### 6.1 Ruler (Plan 9 C, in plan9port)

- **Location**: **`src/cmd/ruler/`** (binary `ruler`). Layout as in §6.0.
- **Behavior**:
  - **Startup**: Ruler starts even if **`$rulerfile`** is unset, missing, or empty. If there are no rules, ruler still runs and responds to queries with an empty payload. When the rulerfile exists, read it from **`$rulerfile`** (config file path only; default `$home/lib/rulerfile` or `~/lib/rulerfile`). **`$rulerfile` is not a connection point**—it is only the path to the rules file.
  - Create the 9P server and **post it** under the service name **ruler** (e.g. `post9pservice(..., "ruler", nil)` so it appears as **`srv/ruler`** in the namespace). Under Unix/plan9port, 9pserve (or the process namespace) makes `srv/name` the standard mount point for services.
  - Expose 9P tree with **`query`** (read/write: request/response) and **`rules`** (read/write: read returns rules as configured and parsed, same as plumber; write reloads from the written content, e.g. `9p write ruler/rules < $HOME/lib/rulerfile`).
  - On write to `query`: parse request (all four attributes **query**, **client**, **event**, **id** are required); match against the loaded rulerfile (query, client, and event); store the matching rule’s directive lines as the response for that fid. On read from `query`: return that response (or empty if no match). Each fid is one request/response pair: write once, read once.
  - Reload on SIGHUP or when a client writes to `rules`.
- **Pattern matching**: implement at least suffix and prefix; regex optional.
- **Include**: when loading the rulerfile, support `include` *file* lines with the semantics in §4.2 (input stack, search path, max depth). Same approach as the plumber (see plan9port `src/cmd/plumb/rules.c` and `man plumb(7)`).
- **Concurrency**: multiple clients may connect; handle concurrent requests (e.g. one fid per request).

### 6.2 Acme changes (Plan 9 C, in plan9port)

- **Where**: Client logic in **`src/cmd/acme/ruler.c`**; call sites in **`exec.c`**, **`look.c`**, **`rows.c`**, **`util.c`**, **`acme.c`** as described in §6.0.
- **Trigger**: When a window’s body has just been associated with a file (Get, new window with file, zerox). Call the ruler client (e.g. `rulerapply(w)`) **before** the corresponding `xfidlog(w, op)` so ruler has precedence and event-log readers see the window after ruler has been applied.
- **Steps** (implemented in `ruler.c`): Resolve **absolute** path for the window’s file; if none or not a regular file, return. Connect to the **ruler service** (§6.3) (e.g. open `ruler/query` via the namespace); if the service is unavailable, return silently. Build request from the shared ruler header (query=absolute path, client=acme, event=open, id=winid; all four required); write to 9P `query`, read response, parse directive lines (first `=` separates key from value), apply Comfmt/Tab/Tabexpand/Indent for known keys. Ignore unknown keys. Caller then publishes the event (e.g. `xfidlog(w, "get")`).
- **No UI change**: no new menus or commands; feature is automatic and best-effort.

### 6.3 Config file and connection

- **Config file (rulerfile path)**: **`$rulerfile`** — path to the **rulerfile** (the rules config file) that the daemon reads at startup. **Not a connection point.** Default: `$home/lib/rulerfile` (Plan 9) or `~/lib/rulerfile` (Unix). Updating the rules is done like the plumber: write the rulerfile content to the 9P file **`ruler/rules`** (e.g. `9p write ruler/rules < $HOME/lib/rulerfile`).
- **Connection (for clients)**: The ruler daemon **posts** itself under the service name **ruler** (e.g. via `post9pservice`), so it appears in the namespace as **`srv/ruler`**. Under Unix/plan9port, services typically post a name and clients connect by opening that name (e.g. `srv/plumb`, `srv/ruler`); the namespace is set up by 9pserve or the process so that `srv/name` resolves to the service’s 9P connection. Acme (and other clients) connect to the ruler service the same way they connect to the plumber: open the service (e.g. `ruler/query`) through the namespace. No separate environment variable is required for the connection unless an override is needed (e.g. `$ruler` for an alternative service path for testing).
- Document in man page and in Acme/Sam docs.

### 6.4 Sam (later)

- Same pattern: when a file is opened (or buffer created with a path), Sam sends `query=<path>`, `client=sam`, `event=open`, `id=...`, reads response, applies Sam-specific directives. Implementation in `src/cmd/sam`, same protocol.

### 6.5 Failure and absence

- If ruler is not running or not configured: clients must not block and must not show errors. Simply skip applying directives (same as “no match”).
- If a directive is unknown or value invalid: ignore that line and continue.

---

## 7. Future extensions (out of scope for initial spec)

- **event=save**: Different rules or directives on save; e.g. “run formatter before writing.”
- **puthook / save hook**: Directive like `puthook=goimports`; editor runs the program on the buffer and replaces with stdout before Put.
- **More clients**: Same protocol, new **client** value; add optional `client=` scope in rulerfile as needed.
- **Daemon pushes**: Optional “reply-to” or callback so the daemon could push updates; would require editors to expose a receive endpoint. Not required for the initial design.

---

## 8. Summary

| Item | Choice |
|------|--------|
| Service name | **ruler** (daemon and 9P tree; binary `ruler`). Posted as **ruler** (srv/ruler). Agnostic to editing; returns configuration from rules for an event. |
| Config file | **`$rulerfile`** — path to rulerfile only (default `$home/lib/rulerfile` or `~/lib/rulerfile`). Not a connection point. Update rules via `9p write ruler/rules < $HOME/lib/rulerfile`. |
| Connection | Clients connect to the **ruler** service (srv/ruler in the namespace), same pattern as plumber (srv/plumb). |
| Config content | **rulerfile** — single file; optional `client=` and `event=` scope per rule. Response content defined by rulerfile; ruler does not interpret it. |
| Protocol (request) | **query**, **client**, **event**, **id** — all four required; ruler matches on query, client, and event. |
| Transport | 9P server posted as ruler (srv/ruler). |
| Request | Write to 9P file `query`: query, client, event, id (one per line). |
| Response | Read from `query`: directive lines from rulerfile (newline-separated `key=value`); opaque to ruler. |
| Implementation | Plan 9 C, all inside plan9port: ruler daemon in `src/cmd/ruler/`; Acme client in `src/cmd/acme/ruler.c`; shared **`include/ruler.h`** for request/response contract. |

This spec is intended to be self-contained so that implementation can be resumed in a future session without relying on the original conversation.

---

## 9. Issues addressed (revision summary)

- **Single rulerfile**: One **rulerfile**; optional `client=` per rule; first match wins. Response content is derived entirely from the rulerfile; ruler does not interpret it.
- **Protocol**: All four request attributes **query**, **client**, **event**, **id** are required. Matching uses query, client, and event; **event=** added to rulerfile rules. Query is not normalized (Acme sends absolute path). First `=` in response lines separates key from value. **query** file: one request/response pair per fid. Ruler starts even with no/empty rulerfile (empty response to queries). Include search path when loading from write: same as plumber.
- **Include**: Rulerfile supports **include** *file* (plumber-style): substitute file contents, input stack, search path (current file’s directory then e.g. `$home/lib/ruler`), max depth 10. See §4.2 and plumb(7) / `src/cmd/plumb/rules.c`.
- **Naming**: Service **ruler**, config file **rulerfile**. **`$rulerfile`** = path to rulerfile only; connection = service **ruler** (srv/ruler). Rules updated via write to **ruler/rules** (like plumber).
- **Typo**: §3.5 “tab width in units of zero character” → “tab width in character units.”
- **Request format**: Request body is one attribute per line (newline-separated).
