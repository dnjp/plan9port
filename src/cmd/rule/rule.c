/*
 * rule — compose and send a ruler query, print the response.
 *
 * Flags set individual request fields:
 *   rule [-q query] [-c client] [-e event] [-i id]
 *
 * If -q is omitted, the query is read from stdin (one line, trailing newline
 * stripped).  If no flags are given at all, the entire raw request is read
 * from stdin (one attribute=value per line), for scripted use.
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <9pclient.h>
#include <ruler.h>

static void
usage(void)
{
	fprint(2, "usage: rule [-q query] [-c client] [-e event] [-i id]\n");
	fprint(2, "       rule -r [rulesfile]  (reload rules; default ~/lib/rules)\n");
	fprint(2, "       rule  (reads raw request from stdin)\n");
	threadexitsall("usage");
}

/* Read one line from fd, stripping the trailing newline. Returns nil on EOF or error. */
static char*
readline(int fd)
{
	static char buf[4096];
	int i = 0;
	char c;

	while(i < (int)sizeof(buf)-1){
		if(read(fd, &c, 1) <= 0)
			break;
		if(c == '\n')
			break;
		buf[i++] = c;
	}
	buf[i] = '\0';
	return buf;
}

void
threadmain(int argc, char **argv)
{
	CFsys *fs;
	CFid *fid;
	char buf[8192];
	long n;
	char *query = nil, *client = nil, *event = nil, *id = nil;
	int anyflag = 0;

	int reload = 0;
	char *rulesfile = nil;

	ARGBEGIN{
	case 'r':
		reload = 1;
		rulesfile = ARGF();	/* optional: nil means use ~/lib/rules */
		break;
	case 'q':
		query = ARGF();
		if(query == nil) usage();
		anyflag = 1;
		break;
	case 'c':
		client = ARGF();
		if(client == nil) usage();
		anyflag = 1;
		break;
	case 'e':
		event = ARGF();
		if(event == nil) usage();
		anyflag = 1;
		break;
	case 'i':
		id = ARGF();
		if(id == nil) usage();
		anyflag = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	if(reload){
		char path[1024];
		int fd;
		CFsys *rfs;
		CFid *rfid;

		if(rulesfile == nil){
			char *home = getenv("HOME");
			if(home == nil)
				sysfatal("rule -r: $HOME not set");
			snprint(path, sizeof path, "%s/lib/rules", home);
			rulesfile = path;
		}
		fd = open(rulesfile, OREAD);
		if(fd < 0)
			sysfatal("rule -r: open %s: %r", rulesfile);
		rfs = nsmount("ruler", nil);
		if(rfs == nil)
			sysfatal("rule -r: mount ruler: %r");
		rfid = fsopen(rfs, "rules", OWRITE);
		if(rfid == nil)
			sysfatal("rule -r: open ruler/rules: %r");
		while((n = read(fd, buf, sizeof buf)) > 0)
			if(fswrite(rfid, buf, n) != n)
				sysfatal("rule -r: write: %r");
		close(fd);
		fsclose(rfid);
		fsunmount(rfs);
		threadexitsall(nil);
	}

	fs = nsmount("ruler", nil);
	if(fs == nil)
		sysfatal("mount ruler: %r");
	fid = fsopen(fs, "query", ORDWR);
	if(fid == nil)
		sysfatal("open ruler/query: %r");

	if(anyflag){
		/* if -q was not given, read the query from stdin */
		if(query == nil)
			query = readline(0);
		n = snprint(buf, sizeof buf,
			"%s=%s\n%s=%s\n%s=%s\n%s=%s\n",
			RulerQuery,  query  ? query  : "",
			RulerClient, client ? client : "",
			RulerEvent,  event  ? event  : "",
			RulerId,     id     ? id     : "");
		if(fswrite(fid, buf, n) != n)
			sysfatal("write: %r");
	} else {
		/* no flags: pass raw stdin directly to the daemon */
		long nw = 0;
		while((n = read(0, buf, sizeof buf)) > 0){
			if(fswrite(fid, buf, n) != n)
				sysfatal("write: %r");
			nw += n;
		}
		if(n < 0)
			sysfatal("read stdin: %r");
		if(nw == 0){
			fprint(2, "rule: no input; use flags or pipe a request to stdin\n");
			fsclose(fid);
			fsunmount(fs);
			threadexitsall("no input");
		}
	}

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
			fprint(2, "rule: no response (no rule matched)\n");
	}

	fsclose(fid);
	fsunmount(fs);
	threadexitsall(nil);
}
