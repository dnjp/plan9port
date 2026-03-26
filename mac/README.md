# macOS integration

This directory contains app bundle and LaunchAgent integration for plan9port.

Primary reference: `mac/MacOS.md`.

## Mailfs daemon template

A per-account `mailfs` LaunchAgent template is provided at:

- `mac/daemons/com.plan9port.mailfs.plist.template`

It is intentionally not part of the default `DAEMONS` set because fresh clones
have no account configuration.

Instantiate/load a daemon with:

```sh
cd /usr/local/plan9/mac
mk mailfs-daemon MAIL_TAG=posteo MAIL_USER=user@example.com MAIL_SERVER=imap.example.com MAIL_SRVNAME=mail.posteo
```

Unload with:

```sh
mk mailfs-daemon-unload MAIL_TAG=posteo
```

### Runtime behavior

The template runs through `mac/daemons/run-with-env.sh`, which provides:

- shared namespace (`NAMESPACE=/tmp/ns.$USER.:0`)
- optional plumber dependency (`P9P_REQUIRE_PLUMB=1`)
- explicit service socket health tracking (`P9P_SERVICE_NAME=<srvname>`)

Use a distinct `MAIL_SRVNAME` per account (for example `mail.posteo`,
`mail.work`) and open Acme Mail with the matching service name:

```sh
/usr/local/plan9/src/cmd/acme/mail/o.Mail -n mail.posteo
```
