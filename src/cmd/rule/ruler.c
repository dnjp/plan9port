#include <u.h>
#include <libc.h>
#include <thread.h>
#include <ruler.h>
#include "ruler.h"

int		verbose;

void
dlog(char *fmt, ...)
{
	va_list a;
	char buf[512];

	if(!verbose)
		return;
	va_start(a, fmt);
	vseprint(buf, buf+sizeof buf, fmt, a);
	va_end(a);
	fprint(2, "ruler: %s", buf);
	if(buf[0] != '\0' && buf[strlen(buf)-1] != '\n')
		fprint(2, "\n");
}

int		foreground;
char		*progname;

static int
sighup(void *v, char *msg)
{
	USED(v);
	if(msg != nil && strstr(msg, "hangup") != nil)
		reloadrulesfromfile();
	return 0;
}

void
reloadrulesfromfile(void)
{
	int fd;
	Rulerule **old;

	if(rulerfile == nil)
		return;
	fd = open(rulerfile, OREAD);
	if(fd < 0)
		return;
	old = rules;
	rules = readrules(rulerfile, fd);
	close(fd);
	if(old != nil)
		freerules(old);
}

int
threadmaybackground(void)
{
	return 1;
}

void
threadmain(int argc, char *argv[])
{
	char buf[512];
	int fd;

	progname = "ruler";

	ARGBEGIN{
	case 'v':
		verbose = 1;
		break;
	case 'f':
		foreground = 1;
		break;
	case 'r':
		rulerfile = estrdup(ARGF());
		break;
	}ARGEND

	user = getuser();
	home = getenv("HOME");
	if(home == nil)
		home = getenv("home");
	if(user == nil)
		user = "nobody";

	if(user==nil || home==nil)
		error("can't initialize $user or $home: %r");
	if(rulerfile == nil){
		sprint(buf, "%s/lib/rules", home);
		if(access(buf, 0) >= 0)
			rulerfile = estrdup(buf);
		else
			rulerfile = unsharp("#9/rule/initial.rules");
	}

	rules = emalloc(sizeof(Rulerule*));
	rules[0] = nil;
	if(rulerfile != nil){
		fd = open(rulerfile, OREAD);
		if(fd >= 0){
			Rulerule **r;
			r = readrules(rulerfile, fd);
			close(fd);
			freerules(rules);
			rules = r;
		}
	}

	{
		int n = 0;
		if(rules != nil)
			for(; rules[n] != nil; n++)
				;
		dlog("rulerfile=%s %d rules loaded", rulerfile ? rulerfile : "(none)", n);
	}
	atnotify(sighup, 1);
	dlog("posting service ruler");
	startfsys(foreground);
	threadexits(nil);
}

void
error(char *fmt, ...)
{
	char buf[512];
	va_list args;

	va_start(args, fmt);
	vseprint(buf, buf+sizeof buf, fmt, args);
	va_end(args);

	fprint(2, "%s: %s\n", progname, buf);
	threadexitsall("error");
}

void*
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		error("malloc failed: %r");
	memset(p, 0, n);
	return p;
}

void*
erealloc(void *p, ulong n)
{
	p = realloc(p, n);
	if(p == nil)
		error("realloc failed: %r");
	return p;
}

char*
estrdup(char *s)
{
	char *t;

	if(s == nil)
		return nil;
	t = strdup(s);
	if(t == nil)
		error("estrdup failed: %r");
	return t;
}
