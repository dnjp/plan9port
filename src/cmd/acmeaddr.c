/*
 * acmeaddr — on a single 9p connection to Acme: write "addr=dot" to the
 * window's ctl, then read and print the window's addr (q0 q1).
 * Usage: acmeaddr [winid]
 * If winid is omitted, uses $winid from the environment (e.g. when Run from Acme).
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <auth.h>
#include <9pclient.h>

static char *winid;

void
threadmain(int argc, char **argv)
{
	CFsys *fs;
	CFid *fid, *ctl;
	char path[64];
	char buf[32];
	long n;

	ARGBEGIN{
	default:
		fprint(2, "usage: acmeaddr [winid]\n");
		threadexitsall("usage");
	}ARGEND

	if(argc > 0)
		winid = argv[0];
	else
		winid = getenv("winid");
	if(winid == nil || winid[0] == '\0'){
		fprint(2, "acmeaddr: no winid (set $winid or pass winid)\n");
		threadexitsall("no winid");
	}

	fs = nsmount("acme", winid);
	if(fs == nil)
		sysfatal("mount acme: %r");

	// open file descriptor and discard the output
	snprint(path, sizeof path, "%s/addr", winid);
	fid = fsopen(fs, path, OREAD);
	if(fid == nil){
		fsunmount(fs);
		sysfatal("open acme/%s/addr: %r", winid);
	}
	while((n = fsread(fid, buf, sizeof buf)) > 0)

	/* write addr=dot to set the current selection addr */
	snprint(path, sizeof path, "%s/ctl", winid);
	ctl = fsopen(fs, path, OWRITE);
	if(ctl == nil){
		fsunmount(fs);
		sysfatal("open acme/%s/ctl: %r", winid);
	}
	if(fswrite(ctl, "addr=dot\n", 9) != 9){
		fsclose(ctl);
		fsunmount(fs);
		sysfatal("write addr=dot: %r");
	}

	// read the addr now that it is set
	snprint(path, sizeof path, "%s/addr", winid);
	fid = fsopen(fs, path, OREAD);
	if(fid == nil){
		fsclose(ctl);
		fsunmount(fs);
		sysfatal("open acme/%s/addr: %r", winid);
	}
	while((n = fsread(fid, buf, sizeof buf)) > 0){
		if(write(1, buf, n) < 0){
			fsclose(fid);
			fsclose(ctl);
			fsunmount(fs);
			sysfatal("write stdout: %r");
		}
	}
	fsclose(fid);
	fsclose(ctl);
	if(n < 0){
		fsunmount(fs);
		sysfatal("read addr: %r");
	}

	fsunmount(fs);
	threadexitsall(nil);
}
