/*
 * rule — write stdin to ruler/query, read response, print to stdout.
 * Same-fid write-then-read so the daemon returns the match.
 * Usage: cat query.txt | rule
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <auth.h>
#include <9pclient.h>

void
threadmain(int argc, char **argv)
{
	CFsys *fs;
	CFid *fid;
	char buf[4096];
	long n;

	ARGBEGIN{
	default:
		fprint(2, "usage: rule (reads query from stdin)\n");
		threadexitsall("usage");
	}ARGEND

	fs = nsmount("ruler", nil);
	if(fs == nil)
		sysfatal("mount ruler: %r");
	fid = fsopen(fs, "query", ORDWR);
	if(fid == nil)
		sysfatal("open ruler/query: %r");

	{
		long nw = 0;
		while((n = read(0, buf, sizeof buf)) > 0){
			if(fswrite(fid, buf, n) != n)
				sysfatal("write: %r");
			nw += n;
		}
		if(n < 0)
			sysfatal("read stdin: %r");
		if(nw == 0){
			fprint(2, "rule: no input (pipe query to stdin, e.g. cat example_query.txt | rule)\n");
			fsclose(fid);
			fsunmount(fs);
			threadexitsall("no input");
		}
	}

	/* response: seek to 0; write left fid at offset */
	fsseek(fid, 0, 0);

	{
		long nr = 0;
		while((n = fsread(fid, buf, sizeof buf)) > 0){
			if(write(1, buf, n) < 0)
				sysfatal("write stdout: %r");
			nr += n;
		}
		if(n < 0)
			sysfatal("read: %r");
		if(nr == 0)
			fprint(2, "rule: no response (ruler matched no rule or returned empty)\n");
	}

	fsclose(fid);
	fsunmount(fs);
	threadexitsall(nil);
}
