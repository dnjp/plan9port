# Mail in plan9port

This directory is the mail configuration layer for this fork of
[plan9port](https://github.com/9fans/plan9port).  It provides a multi-account
IMAP/SMTP setup built entirely on native upas tools: no msmtp, no Thunderbird,
no external mail agents.

---

## How mail works in plan9port

Plan 9's mail system is built from a small set of cooperating programs.
Understanding the data flow for both receiving and sending is essential before
configuring anything.

### Programs

| Program | Role |
|---|---|
| `upas/mailfs` | IMAP client that presents an inbox as a 9P filesystem |
| `Mail` (Acme) | Mail reader and composer UI, built on acme's 9P interface |
| `upas/marshal` | Message composer/formatter; the entry point for sending |
| `upas/send` | Address rewriting, queuing, and local delivery |
| `upas/vf` | Virus filter; validates attachments via `lib/validateattachment` |
| `upas/qer` | Queues a message for background delivery |
| `upas/runq` | Drains the queue by invoking `lib/remotemail` per message |
| `upas/smtp` | Speaks SMTP; called by `remotemail` via stunnel for TLS |
| `factotum` | Authentication agent; supplies IMAP and SMTP credentials |
| `stunnel` | Provides TLS wrapping for SMTP submission (port 587) — **third-party** |

`stunnel` is not part of plan9port and must be installed separately.  On macOS,
install it with the rest of the macOS-specific dependencies:

```sh
cd $PLAN9/mac && mk install-deps   # installs Brewfile (includes stunnel, duti, etc.)
```

Or directly:

```sh
brew install stunnel
```

### Receive path

```
IMAP server
  → mailfs -s <srvname> -t -u <user> <imap-host>
      posts 9P service at $NAMESPACE/<srvname>
  → Mail -n <srvname>
      mounts the 9P service and presents the inbox in Acme
```

`mailfs` authenticates via `factotum` using a key matching:
```
proto=pass service=imap server=<imap-host> user=<address>
```

### Send path

```
Mail (compose window)  or  echo msg | upas/marshal
  → $MAILROOT/pipefrom   (if executable; injects From: header)
      → upas/send
          → $UPASLIB/rewrite   (address rewriting rules)
              → $UPASLIB/qmail  (| pipe action; buffers, filters, queues)
                  → upas/vf    (attachment validation)
                  → upas/qer   (writes C.*/D.*  queue files)
                  → upas/runq  (background drain, calls remotemail per message)
                      → $UPASLIB/remotemail
                          → stunnel3 … | upas/smtp -a -u <user> <host> …
                                          (authenticates via factotum smtp key)
```

`upas/marshal` success means only that the message entered the pipeline.
Actual SMTP delivery is completed asynchronously by `runq`.  Check `E.*` files
in the queue directory if messages do not arrive.

### Key environment variables

`UPASLIB` and `MAILROOT` default to `$PLAN9/mail/lib` and `$PLAN9/mail`
respectively.  You only need to set them explicitly if you move the mail
directory elsewhere.  They are documented (commented out) in the shell
templates under `$PLAN9/shell/`:

| Template | Variable block |
|---|---|
| `shell/plan9.sh.in` → `plan9.sh` | `MAILROOT` / `UPASLIB` (bash/zsh/sh) |
| `shell/plan9.fish.in` → `plan9.fish` | `MAILROOT` / `UPASLIB` (fish) |
| `shell/plan9.rc.in` → `plan9.rc` | `MAILROOT` / `UPASLIB` (rc) |

To override, uncomment and edit those lines, or source `$PLAN9/shell/mail.env`
after editing it.

| Variable | Meaning | Default |
|---|---|---|
| `PLAN9` | plan9port installation root | `/usr/local/plan9` |
| `MAILROOT` | mail root (queue, accounts, pipefrom, etc.) | `$PLAN9/mail` |
| `UPASLIB` | upas library dir (rewrite, qmail, remotemail, etc.) | `$PLAN9/mail/lib` |
| `NAMESPACE` | 9P socket directory shared by all plan9port tools | `/tmp/ns.$USER.:0` |

### `rewrite` rules

`upas/send` processes each recipient address through `$UPASLIB/rewrite`.
The most important action is `|` (pipe), which pipes the full message to
`$UPASLIB/qmail`.  `qmail` reads the `From:` header to pick the correct
SMTP relay from `$MAILROOT/accounts`, so routing is always sender-based
regardless of recipient domain.

### `lib/ignore`

Lists header prefixes that `mailfs` suppresses from display in Acme Mail.
Loaded from the POSIX path `/mail/lib/ignore` (hardcoded in `src/cmd/upas/fs/fs.c`).
On macOS, `/mail` is not a real filesystem path unless created explicitly —
this is a known limitation; a future patch to upas should look in `$PLAN9/mail/lib`
instead.

### `lib/validateattachment`

Called by `upas/vf` for each MIME attachment.  Resolves to
`$PLAN9/mail/lib/validateattachment` via `unsharp("#9/mail/lib/...")`.  Exit codes:

| Exit code | Meaning |
|---|---|
| `10` | Accept — pass through unchanged |
| `13` | Discard — refuse the message |
| anything else | Wrap — rename attachment to `.suspect` extension |

### `lib/namefiles`

Lists alias database files for `aliasmail`.  `aliasmail` `chdir`s to
`$UPASLIB` before opening `namefiles`, so it respects `UPASLIB`.  The default
entry is `names.local`.  Add alias entries to `$UPASLIB/names.local`:

```
me: you@provider.example
```

### Factotum keys

`mailfs` and `upas/smtp` both ask `factotum` for credentials at runtime.
Keys must be loaded before either program can authenticate:

```
key proto=pass service=imap  server=<imap-host>  user=<address> !password=<pw>
key proto=pass service=smtp  server=<smtp-host>  user=<address> !password=<pw>
```

Keys are stored encrypted in `~/lib/factotum` (AES-CBC via `aescbc`).  On
macOS, the passphrase is stored in Keychain under `plan9port-factotum` so the
factotum LaunchAgent can load keys automatically at login.  On other platforms,
the passphrase is prompted interactively.

```sh
# One-time: create the encrypted key file
cd $PLAN9/mail && mk keys

# Edit the key file interactively (any platform)
cd $PLAN9/mail && mk keysedit
```

`mk auth` (run automatically by `./SETUP`) also writes credentials directly to
the key file after loading them into factotum.

---

## This fork's configuration layer

### File map

| Path | Tracked? | Purpose |
|---|---|---|
| `SETUP` | ✓ | Reads `accounts`, generates rewrite/remotemail, runs mk per account |
| `$PLAN9/shell/mail.env` | ✓ | Optional env override; source only if relocating the mail dir |
| `mkfile` | ✓ | Helper targets: `account`, `auth`, `start`, `daemon-load`, etc. |
| `pipefrom` | ✓ | Intercepts `marshal` output; injects `From:` header from `accounts` |
| `accounts.example` | ✓ | Template for account config blocks (copy to `accounts`) |
| `accounts` | ✗ | Local account config; gitignored — contains real addresses |
| `lib/qmail` | ✓ | Queuing script; reads `From:` to pick SMTP relay from `accounts` |
| `lib/rewrite` | ✗ | Generated rewrite rules; gitignored — produced by `SETUP` |
| `lib/remotemail` | ✗ | Delivery script called by `runq`; gitignored — produced by `SETUP` |
| `lib/ignore` | ✓ | Header suppression list for `mailfs` |
| `lib/namefiles` | ✓ | Alias database index for `aliasmail` |
| `lib/names.local` | ✓ | Local alias definitions |
| `lib/validateattachment` | ✓ | Attachment checker called by `upas/vf` |
| `queue/` | ✗ | Mail queue directory; gitignored |
| `runq.log` | ✗ | Queue drain log; gitignored |

### `SETUP`

The primary entry point for account management.  It reads `$PLAN9/mail/accounts`,
generates `lib/rewrite` and `lib/remotemail` from all account blocks, then runs
`mk <target>` for each matching account with all required variables set.

```sh
./SETUP                       # account + auth + daemon-load for every account
./SETUP account work          # full setup for the 'work' account only
./SETUP auth work             # reload credentials for 'work'
./SETUP daemon-load all       # (re)start all account daemons
./SETUP refresh work          # force inbox refresh for 'work'
```

### `pipefrom`

`upas/marshal` checks `$MAILROOT/pipefrom` before invoking `upas/send`.  This
fork uses it to determine the sending account (from `MAIL_SRVNAME`, `MAIL_ACCOUNT`,
the `$replymsg` path set by `marshal` for replies, or the first account in
`accounts`), look up the display name, inject or rewrite the `From:` header, and
delegate to `upas/send`.

### Multi-account model

Each account gets:
- A mailfs process with its own `-s <srvname>` socket (macOS: LaunchAgent).
- A factotum key pair (`service=imap` + `service=smtp`).
- A block in `accounts` with `name`, `display`, `account`, `imap`, `smtp`, `srv`.

All accounts share `lib/qmail`, `lib/rewrite`, and `lib/remotemail` because
routing is driven by the `From:` header at send time.

---

## One-time setup

### 1. Install dependencies

On macOS, install the Brewfile from `$PLAN9/mac/` (includes stunnel, duti, etc.):

```sh
cd $PLAN9/mac && mk install-deps
```

On other platforms, install stunnel via your package manager.

### 2. Build and install upas binaries

```sh
cd $PLAN9/src/cmd/upas && mk install
cd $PLAN9/src/cmd/acme/mail && mk all
```

Install order matters on macOS case-insensitive filesystems: install upas
first, then Acme mail, so `$PLAN9/bin/Mail` is the Acme mail client.

### 3. Create your accounts file

```sh
cd $PLAN9/mail
cp accounts.example accounts
$EDITOR accounts   # fill in real addresses and hostnames
```

### 4. Run SETUP

```sh
cd $PLAN9/mail
./SETUP
```

With no arguments, `SETUP` sets up every account block in `accounts` (generates
rewrite/remotemail, loads factotum credentials, starts mailfs).  You can also
target a specific account or mk target:

```sh
./SETUP account work          # set up only the 'work' account
./SETUP auth work             # reload credentials only
./SETUP daemon-load all       # (re)start all daemons
```

---

## Daily usage

Open Acme Mail for an account (use the `srv` value from `accounts`):

```sh
Mail -n mail.work
```

Force an inbox refresh:

```sh
cd $PLAN9/mail && mk refresh MAIL_SRVNAME=mail.work ACCOUNT=... IMAPSERVER=... SMTPSERVER=... MAIL_TAG=work
# or via SETUP:
./SETUP refresh work
```

Drain the outgoing queue:

```sh
cd $PLAN9/mail && mk drainqueue
```

---

## Starting mailfs manually

`mk start` runs mailfs directly and prints the command for future reference
(useful on non-macOS platforms or when not using LaunchAgents):

```sh
cd $PLAN9/mail && mk start ACCOUNT=you@work.example IMAPSERVER=imap.work.example MAIL_SRVNAME=mail.work MAIL_TAG=work SMTPSERVER=smtp.work.example
```

The printed command (`mailfs -s mail.work -t -u you@work.example imap.work.example`)
can be put in a startup script or rc profile for systems without a daemon manager.

On macOS, `daemon-load` uses LaunchAgents for automatic restart on login:

```sh
./SETUP daemon-load work
```

---

## Adding a new account

### 1. Add a block in `accounts`

```
name=work
display=Your Name
account=you@work.example
imap=imap.work.example
smtp=smtp.work.example
srv=mail.work
```

### 2. Run setup for the new account

```sh
cd $PLAN9/mail
./SETUP account work
```

This regenerates `lib/rewrite` and `lib/remotemail` for all accounts, loads
factotum credentials for the new one, and starts its mailfs daemon.

### 3. Open Mail for the new account

```sh
Mail -n mail.work
```

---

## Sending a test message

```sh
echo 'test body' | $PLAN9/bin/upas/marshal -s 'test subject' you@recipient.example
cd $PLAN9/mail && mk drainqueue
```

If the message does not arrive, inspect the queue:

```sh
ls -la $PLAN9/mail/queue/$USER
```

Key queue files:

| File | Contents |
|---|---|
| `C.*` | Control file: delivery command and arguments |
| `D.*` | Message data |
| `E.*` | Last error output from a failed delivery attempt |

Force a retry ignoring backoff timers:

```sh
$PLAN9/bin/upas/runq -E -n 20 $PLAN9/mail/queue $PLAN9/mail/lib/remotemail
```

---

## Factotum key management

```sh
# One-time: create the encrypted key file and store the passphrase
cd $PLAN9/mail && mk keys

# Interactive edit (any platform — uses ipso -a)
cd $PLAN9/mail && mk keysedit
```

`mk auth` (called by `./SETUP`) also writes credentials directly to the key file.

---

## Common pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| `Mail -n` shows empty inbox | Wrong service name | Use the `srv` value from `accounts` |
| Send succeeds but nothing arrives | `runq` backoff on `E.*` files | Run `runq -E` to force retry; inspect `E.*` for the error |
| `554 Sender address rejected` | Envelope sender is `user@hostname.local` | `remotemail` normalizes bare senders; re-run `./SETUP` to regenerate it |
| `450 too many requests` | SMTP server rate-limited after many retries | Wait for the rate limit window; purge stale `E.*` files |
| Second `mailfs` kills first | `run-with-env.sh` used `pkill -x mailfs` (too broad) | Fixed: uses `pkill -f "mailfs.*$svc"` when `P9P_SERVICE_NAME` is set |
| Headers all visible in Mail | `ignore` not read (requires `/mail/lib/ignore` as a POSIX path) | Known macOS limitation; planned fix: upas should look in `$PLAN9/mail/lib` |
| `Mail` binary is wrong program | Install order: upas Mail installed over Acme Mail | Reinstall: `cd $PLAN9/src/cmd/upas && mk install`, then `cd $PLAN9/src/cmd/acme/mail && mk all` |
| factotum keys lost after reboot | Keys only in factotum memory | Run `mk keys` once; thereafter `mk auth` / `./SETUP auth` updates the key file automatically |
| stunnel not found | Third-party dependency not installed | `brew install stunnel` or `cd $PLAN9/mac && mk install-deps` |

---

## Original upstream README

The following is the original `README` from 9fans/plan9port, preserved for
historical reference:

```
To run mail you need to do at least the following:

	cd $PLAN9/src/cmd/upas; mk install
	cd $PLAN9/log; chmod 666 smtp smtp.debug smtp.fail mail >smtp >smtp.debug >smtp.fail >mail
	chmod 777 $PLAN9/mail/queue
```
