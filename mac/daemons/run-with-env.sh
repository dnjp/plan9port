#!/usr/bin/env bash
# Run a plan9port daemon with the user's login environment.
# Sets NAMESPACE explicitly so all daemons share the same 9p socket directory
# as interactive terminals, regardless of when LaunchAgents start.
#
# These daemons (plumber, fontsrv, ruler) self-daemonize by forking 9pserve
# as a child and exiting. macOS LaunchAgents kill the process group on exit,
# which would take 9pserve with it. We prevent this by running the daemon in
# a new session (setsid) so 9pserve is detached from our process group, then
# keeping this wrapper alive so launchd considers the job still running.

set -e

for f in "$HOME/.profile" "$HOME/.bash_profile"; do
	if [[ -f "$f" ]]; then
		# shellcheck source=/dev/null
		source "$f"
		break
	fi
done

PLAN9=${PLAN9:-/usr/local/plan9}

# Use the canonical namespace directory (same as namespace(1) with no $DISPLAY).
# This matches what interactive plan9port sessions use on macOS.
export NAMESPACE="/tmp/ns.${USER}.:0"
mkdir -p "$NAMESPACE"

# Determine the 9p socket name for this daemon.
if [[ $# -ge 1 ]]; then
	bin=$(basename "$1")
	case "$bin" in
		fontsrv)  svc=font  ;;
		plumber)  svc=plumb ;;
		*)        svc=$bin  ;;
	esac

	# Kill any existing instance so we can bind the socket cleanly.
	pkill -x "$bin" 2>/dev/null || true
	pkill -f "9pserve.*/$svc$" 2>/dev/null || true
	sleep 0.2
	rm -f "$NAMESPACE/$svc"

	# Start the daemon detached from our process group so its 9pserve child
	# won't be killed when launchd reaps this wrapper.
	# macOS has no setsid(1); we use a subshell with job control disabled.
	(set +m; "$@" &)

	# Wait for the socket to appear (up to 5 seconds).
	for i in 1 2 3 4 5; do
		sleep 1
		[[ -S "$NAMESPACE/$svc" ]] && break
	done
fi

# Keep this wrapper alive so launchd tracks the job. The actual work is done
# by the detached 9pserve process. We sleep in a loop so SIGTERM from
# launchd unload is handled cleanly.
while true; do sleep 60; done
