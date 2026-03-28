# macOS integration

This directory contains macOS-specific integration for plan9port: app bundles,
LaunchAgent daemons, and helper scripts.

Primary reference: `mac/MacOS.md`.

## Dependencies

A `Brewfile` is provided with all macOS-specific third-party dependencies:

```sh
cd $PLAN9/mac
brew bundle   # installs stunnel, duti, and other tools
```

Or install the key tools individually:

```sh
brew install stunnel   # TLS wrapper used by upas/smtp for SMTP submission
brew install duti      # sets default app handlers for file types via Plumb.app
```

## Daemons

The core plan9port daemons (fontsrv, plumber, ruler, factotum) are managed as
LaunchAgents.  Install and load them with:

```sh
cd $PLAN9/mac && mk install && mk load
```

See `mac/MacOS.md` for full lifecycle documentation.

## Mailfs daemon template

A per-account `mailfs` LaunchAgent template lives at:

```
mac/daemons/com.plan9port.mailfs.plist.template
```

It is not part of the default `DAEMONS` set because fresh clones have no
account configuration.  Mailfs daemons are managed from `$PLAN9/mail/`:

```sh
cd $PLAN9/mail
./SETUP daemon-load work        # install + start daemon for 'work' account
./SETUP daemon-unload work      # stop and uninstall daemon for 'work' account
```

Or directly via mk (with account variables set explicitly):

```sh
cd $PLAN9/mac
mk mailfs-daemon MAIL_TAG=work MAIL_USER=you@work.example MAIL_SERVER=imap.work.example MAIL_SRVNAME=mail.work
mk mailfs-daemon-unload MAIL_TAG=work
```

### Runtime behaviour

Each mailfs daemon runs through `mac/daemons/run-with-env.sh`, which provides:

- Shared namespace (`NAMESPACE=/tmp/ns.$USER.:0`).
- Optional plumber dependency (`P9P_REQUIRE_PLUMB=1`) — mailfs waits for the
  plumber socket before starting.
- Per-instance kill targeting via `P9P_SERVICE_NAME` — prevents one mailfs
  daemon from killing another when multiple accounts run simultaneously.

Use a distinct `MAIL_SRVNAME` per account (e.g. `mail.work`, `mail.personal`)
and open Acme Mail with the matching name:

```sh
Mail -n mail.work
```

## Factotum key management

Factotum credential persistence (aescbc-encrypted `~/lib/factotum`, Keychain
passphrase) is managed from `$PLAN9/mail/` since it is not macOS-specific:

```sh
cd $PLAN9/mail
mk keys       # one-time: create key file, store passphrase in Keychain
mk keysedit   # edit key file interactively via ipso
```

On macOS, the Keychain passphrase is stored automatically by `mk keys` and
retrieved at login by `mac/daemons/load-factotum-keys.sh` (invoked by the
factotum LaunchAgent via `P9P_POST_START_HOOK`).
