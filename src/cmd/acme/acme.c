#include <u.h>
#include <libc.h>
#include <stdarg.h>
#include <stdio.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"
	/* for generating syms in mkfile only: */
	#include <bio.h>
	#include "edit.h"

void	mousethread(void*);
void	keyboardthread(void*);
void	waitthread(void*);
void	xfidallocthread(void*);
void	newwindowthread(void*);
void	plumbproc(void*);
void	themeproc(void*);
void	rowupdatecols(Row *r);
int	timefmt(Fmt*);

Reffont	**fontcache;
int		nfontcache;
int		themefd;
char		wdir[512] = ".";
Reffont	*reffonts[2];
int		snarffd = -1;
int		mainpid;
int		swapscrollbuttons = FALSE;
char		*mtpt;
Colors		*curtheme;

enum{
	NSnarf = 1000	/* less than 1024, I/O buffer size */
};
Rune	snarfrune[NSnarf+1];

char		*fontnames[2] =
{
	"/lib/font/bit/lucsans/euro.8.font",
	"/lib/font/bit/lucm/unicode.9.font"
};

Command *command;

void	shutdownthread(void*);
void	acmeerrorinit(void);
void	readfile(Column*, char*);
static int	shutdown(void*, char*);

void
derror(Display *d, char *errorstr)
{
	USED(d);
	error(errorstr);
}

static FILE *acmelogf = NULL;
static void
acmelog(const char *fmt, ...)
{
	if(acmelogf == NULL)
		acmelogf = fopen("/tmp/acme-shutdown.log", "a");
	if(acmelogf){
		va_list ap;
		va_start(ap, fmt);
		vfprintf(acmelogf, fmt, ap);
		va_end(ap);
		fflush(acmelogf);
	}
}

void
threadmain(int argc, char *argv[])
{
	int i;
	char *p, *loadfile;
	Column *c;
	int ncol;
	Display *d;

	rfork(RFENVG|RFNAMEG);

	ncol = -1;

	loadfile = nil;
	ARGBEGIN{
	case 'D':
		{extern int _threaddebuglevel;
		_threaddebuglevel = ~0;
		}
		break;
	case 'a':
		globalautoindent = TRUE;
		break;
	case 'b':
		bartflag = TRUE;
		break;
	case 'c':
		p = ARGF();
		if(p == nil)
			goto Usage;
		ncol = atoi(p);
		if(ncol <= 0)
			goto Usage;
		break;
	case 'f':
		fontnames[0] = ARGF();
		if(fontnames[0] == nil)
			goto Usage;
		break;
	case 'F':
		fontnames[1] = ARGF();
		if(fontnames[1] == nil)
			goto Usage;
		break;
	case 'l':
		loadfile = ARGF();
		if(loadfile == nil)
			goto Usage;
		break;
	case 'm':
		mtpt = ARGF();
		if(mtpt == nil)
			goto Usage;
		break;
	case 'r':
		swapscrollbuttons = TRUE;
		break;
	case 'W':
		winsize = ARGF();
		if(winsize == nil)
			goto Usage;
		break;
	default:
	Usage:
		fprint(2, "usage: acme -a -c ncol -f fontname -F fixedwidthfontname -l loadfile -W winsize\n");
		threadexitsall("usage");
	}ARGEND

	fontnames[0] = estrdup(fontnames[0]);
	fontnames[1] = estrdup(fontnames[1]);

	quotefmtinstall();
	fmtinstall('t', timefmt);

	cputype = getenv("cputype");
	objtype = getenv("objtype");
	home = getenv("HOME");
	acmeshell = getenv("acmeshell");
	if(acmeshell && *acmeshell == '\0')
		acmeshell = nil;
	p = getenv("tabstop");
	if(p != nil){
		maxtab = strtoul(p, nil, 0);
		free(p);
	}
	if(maxtab == 0)
		maxtab = 4;
	p = getenv("tabexpand");
	if(p != nil && *p != '\0'){
		tabexpand = TRUE;
	}
	p = getenv("comfmt");
	if(p != nil && *p != '\0'){
		Rune *r;
		int nr;
		r = bytetorune(p, &nr);
		setcomfmt(r, nr);
		free(r);
	}
	if(loadfile)
		rowloadfonts(loadfile);
	putenv("font", fontnames[0]);
	snarffd = open("/dev/snarf", OREAD|OCEXEC);
/*
	if(cputype){
		sprint(buf, "/acme/bin/%s", cputype);
		bind(buf, "/bin", MBEFORE);
	}
	bind("/acme/bin", "/bin", MBEFORE);
*/
	getwd(wdir, sizeof wdir);

/*
	if(geninitdraw(nil, derror, fontnames[0], "acme", nil, Refnone) < 0){
		fprint(2, "acme: can't open display: %r\n");
		threadexitsall("geninitdraw");
	}
*/
	if(initdraw(derror, fontnames[0], "acme") < 0){
		fprint(2, "acme: can't open display: %r\n");
		threadexitsall("initdraw");
	}
	drawsetlabel(wdir);

	d = display;
	font = d->defaultfont;
/*assert(font); */

	reffont.f = font;
	reffonts[0] = &reffont;
	incref(&reffont.ref);	/* one to hold up 'font' variable */
	incref(&reffont.ref);	/* one to hold up reffonts[0] */
	fontcache = emalloc(sizeof(Reffont*));
	nfontcache = 1;
	fontcache[0] = &reffont;

	/* pre-load fixed font if -F was given, so rfget(1,...) never calls openfont at open time */
	if(fontnames[1] != nil && strcmp(fontnames[0], fontnames[1]) != 0){
		Reffont *rf1 = rfget(1, TRUE, FALSE, nil);
		/* warm the subfont cache so the first file open doesn't pay the fontsrv spawn cost */
		if(rf1 != nil)
			stringwidth(rf1->f, "abcdefghijklmnopqrstuvwxyz0123456789");
	}

	iconinit();
	timerinit();
	rxinit();

	cwait = threadwaitchan();
	ccommand = chancreate(sizeof(Command**), 0);
	ckill = chancreate(sizeof(Rune*), 0);
	cxfidalloc = chancreate(sizeof(Xfid*), 0);
	cxfidfree = chancreate(sizeof(Xfid*), 0);
	cnewwindow = chancreate(sizeof(Channel*), 0);
	cerr = chancreate(sizeof(char*), 0);
	cedit = chancreate(sizeof(int), 0);
	cexit = chancreate(sizeof(int), 0);
	cwarn = chancreate(sizeof(void*), 1);
	ctheme = chancreate(sizeof(ulong), 1);
	if(cwait==nil || ccommand==nil || ckill==nil || cxfidalloc==nil || cxfidfree==nil || cerr==nil || cexit==nil || cwarn==nil || ctheme==nil){
		fprint(2, "acme: can't create initial channels: %r\n");
		threadexitsall("channels");
	}
	chansetname(ccommand, "ccommand");
	chansetname(ckill, "ckill");
	chansetname(cxfidalloc, "cxfidalloc");
	chansetname(cxfidfree, "cxfidfree");
	chansetname(cnewwindow, "cnewwindow");
	chansetname(cerr, "cerr");
	chansetname(cedit, "cedit");
	chansetname(cexit, "cexit");
	chansetname(cwarn, "cwarn");
	chansetname(ctheme, "ctheme");

	mousectl = initmouse(nil, screen);
	if(mousectl == nil){
		fprint(2, "acme: can't initialize mouse: %r\n");
		threadexitsall("mouse");
	}
	mouse = &mousectl->m;
	keyboardctl = initkeyboard(nil);
	if(keyboardctl == nil){
		fprint(2, "acme: can't initialize keyboard: %r\n");
		threadexitsall("keyboard");
	}
	mainpid = getpid();
	startplumbing();
/*
	plumbeditfd = plumbopen("edit", OREAD|OCEXEC);
	if(plumbeditfd < 0)
		fprint(2, "acme: can't initialize plumber: %r\n");
	else{
		cplumb = chancreate(sizeof(Plumbmsg*), 0);
		threadcreate(plumbproc, nil, STACK);
	}
	plumbsendfd = plumbopen("send", OWRITE|OCEXEC);
*/

	fsysinit();

	#define	WPERCOL	8
	disk = diskinit();
	if(!loadfile || !rowload(&row, loadfile, TRUE)){
		rowinit(&row, screen->clipr);
		if(ncol < 0){
			if(argc == 0)
				ncol = 2;
			else{
				ncol = (argc+(WPERCOL-1))/WPERCOL;
				if(ncol < 2)
					ncol = 2;
			}
		}
		if(ncol == 0)
			ncol = 2;
		for(i=0; i<ncol; i++){
			c = rowadd(&row, nil, -1);
			if(c==nil && i==0)
				error("initializing columns");
		}
		c = row.col[row.ncol-1];
		if(argc == 0)
			readfile(c, wdir);
		else
			for(i=0; i<argc; i++){
				p = utfrrune(argv[i], '/');
				if((p!=nil && strcmp(p, "/guide")==0) || i/WPERCOL>=row.ncol)
					readfile(c, argv[i]);
				else
					readfile(row.col[i/WPERCOL], argv[i]);
			}
	}
	flushimage(display, 1);

	themefd = themewatchfd();
	if(themefd >= 0)
		proccreate(themeproc, nil, 4096);

	acmeerrorinit();
	threadcreate(keyboardthread, nil, STACK);
	threadcreate(mousethread, nil, STACK);
	threadcreate(waitthread, nil, STACK);
	threadcreate(xfidallocthread, nil, STACK);
	threadcreate(newwindowthread, nil, STACK);
/*	threadcreate(shutdownthread, nil, STACK); */
	acmelog("acme: registering threadnotify, pid=%d\n", (int)getpid());
	threadnotify(shutdown, 1);
	recvul(cexit);
	killprocs();
	threadexitsall(nil);
}

void
readfile(Column *c, char *s)
{
	Window *w;
	Rune rb[256];
	int nr;
	Runestr rs;

	w = coladd(c, nil, nil, -1);
	if(s[0] != '/')
		runesnprint(rb, sizeof rb, "%s/%s", wdir, s);
	else
		runesnprint(rb, sizeof rb, "%s", s);
	nr = runestrlen(rb);
	rs = cleanrname(runestr(rb, nr));
	winsetname_contract(w, rs.r, rs.nr);
	textload(&w->body, 0, s, 1);
	w->body.file->mod = FALSE;
	w->dirty = FALSE;
	winsettag(w);
	winresize(w, w->r, FALSE, TRUE);
	textscrdraw(&w->body);
	textsetselect(&w->tag, w->tag.file->b.nc, w->tag.file->b.nc);
	rulerapply(w);
	xfidlog(w, "new");
}

char *ignotes[] = {
	"sys: write on closed pipe",
	"sys: ttin",
	"sys: ttou",
	"sys: tstp",
	"sys: child",
	nil
};

char *oknotes[] ={
	"delete",
	"hangup",
	"kill",
	"exit",
	nil
};

int	dumping;

static int
shutdown(void *v, char *msg)
{
	int i;

	USED(v);

	acmelog("acme: shutdown called with msg='%s' pid=%d\n", msg, getpid());

	for(i=0; ignotes[i]; i++)
		if(strncmp(ignotes[i], msg, strlen(ignotes[i])) == 0){
			acmelog("acme: ignoring note '%s'\n", msg);
			return 1;
		}

	killprocs();
	if(!dumping && strcmp(msg, "kill")!=0 && strcmp(msg, "exit")!=0 && getpid()==mainpid){
		dumping = TRUE;
		rowdump(&row, nil);
	}
	for(i=0; oknotes[i]; i++)
		if(strncmp(oknotes[i], msg, strlen(oknotes[i])) == 0)
			threadexitsall(msg);
	print("acme: %s\n", msg);
	return 0;
}

/*
void
shutdownthread(void *v)
{
	char *msg;
	Channel *c;

	USED(v);

	threadsetname("shutdown");
	c = threadnotechan();
	while((msg = recvp(c)) != nil)
		shutdown(nil, msg);
}
*/

void
killprocs(void)
{
	Command *c;

	fsysclose();
/*	if(display) */
/*		flushimage(display, 1); */

	for(c=command; c; c=c->next)
		postnote(PNGROUP, c->pid, "hangup");
}

static int errorfd;
int erroutfd;

void
acmeerrorproc(void *v)
{
	char *buf;
	int n;

	USED(v);
	threadsetname("acmeerrorproc");
	buf = emalloc(8192+1);
	while((n=read(errorfd, buf, 8192)) >= 0){
		buf[n] = '\0';
		sendp(cerr, estrdup(buf));
	}
	free(buf);
}

void
acmeerrorinit(void)
{
	int pfd[2];

	if(pipe(pfd) < 0)
		error("can't create pipe");
#if 0
	sprint(acmeerrorfile, "/srv/acme.%s.%d", getuser(), mainpid);
	fd = create(acmeerrorfile, OWRITE, 0666);
	if(fd < 0){
		remove(acmeerrorfile);
  		fd = create(acmeerrorfile, OWRITE, 0666);
		if(fd < 0)
			error("can't create acmeerror file");
	}
	sprint(buf, "%d", pfd[0]);
	write(fd, buf, strlen(buf));
	close(fd);
	/* reopen pfd[1] close on exec */
	sprint(buf, "/fd/%d", pfd[1]);
	errorfd = open(buf, OREAD|OCEXEC);
#endif
	fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
	fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
	erroutfd = pfd[0];
	errorfd = pfd[1];
	if(errorfd < 0)
		error("can't re-open acmeerror file");
	proccreate(acmeerrorproc, nil, STACK);
}

/*
void
plumbproc(void *v)
{
	Plumbmsg *m;

	USED(v);
	threadsetname("plumbproc");
	for(;;){
		m = threadplumbrecv(plumbeditfd);
		if(m == nil)
			threadexits(nil);
		sendp(cplumb, m);
	}
}
*/

void themeproc(void *v) {
	USED(v);
	char buf[16];
	for(;;) {
		read(themefd, buf, sizeof buf);  /* blocks until OS flips */
		sendul(ctheme, 1);
	}
}

void
keyboardthread(void *v)
{
	Rune r;
	Timer *timer;
	Text *t;
	enum { KTimer, KKey, NKALT };
	static Alt alts[NKALT+1];

	USED(v);
	alts[KTimer].c = nil;
	alts[KTimer].v = nil;
	alts[KTimer].op = CHANNOP;
	alts[KKey].c = keyboardctl->c;
	alts[KKey].v = &r;
	alts[KKey].op = CHANRCV;
	alts[NKALT].op = CHANEND;

	timer = nil;
	typetext = nil;
	threadsetname("keyboardthread");
	for(;;){
		switch(alt(alts)){
		case KTimer:
			timerstop(timer);
			t = typetext;
			if(t!=nil && t->what==Tag){
				winlock(t->w, 'K');
				wincommit(t->w, t);
				winunlock(t->w);
				flushimage(display, 1);
			}
			alts[KTimer].c = nil;
			alts[KTimer].op = CHANNOP;
			break;
		case KKey:
		casekeyboard:
			typetext = rowtype(&row, r, mouse->xy);
			t = typetext;
			if(t!=nil && t->col!=nil && !(r==Kdown || r==Kup || r==Kleft || r==Kright))	/* scrolling/cursor move doesn't change activecol */
				activecol = t->col;
			if(t!=nil && t->w!=nil)
				t->w->body.file->curtext = &t->w->body;
			if(timer != nil)
				timercancel(timer);
			if(t!=nil && t->what==Tag) {
				timer = timerstart(500);
				alts[KTimer].c = timer->c;
				alts[KTimer].op = CHANRCV;
			}else{
				timer = nil;
				alts[KTimer].c = nil;
				alts[KTimer].op = CHANNOP;
			}
			if(nbrecv(keyboardctl->c, &r) > 0)
				goto casekeyboard;
			flushimage(display, 1);
			break;
		}
	}
}

void
mousethread(void *v)
{
	Text *t, *argt;
	int but;
	uint q0, q1;
	Window *w;
	Plumbmsg *pm;
	Mouse m;
	char *act;
	enum { MResize, MMouse, MPlumb, MWarnings, NMALT };
	enum { Shift = 5 };
	static Alt alts[NMALT+1];

	USED(v);
	threadsetname("mousethread");
	alts[MResize].c = mousectl->resizec;
	alts[MResize].v = nil;
	alts[MResize].op = CHANRCV;
	alts[MMouse].c = mousectl->c;
	alts[MMouse].v = &mousectl->m;
	alts[MMouse].op = CHANRCV;
	alts[MPlumb].c = cplumb;
	alts[MPlumb].v = &pm;
	alts[MPlumb].op = CHANRCV;
	alts[MWarnings].c = cwarn;
	alts[MWarnings].v = nil;
	alts[MWarnings].op = CHANRCV;
	if(cplumb == nil)
		alts[MPlumb].op = CHANNOP;
	alts[NMALT].op = CHANEND;

	for(;;){
		qlock(&row.lk);
		flushwarnings();
		qunlock(&row.lk);
		flushimage(display, 1);
		switch(alt(alts)){
		case MResize:
			if(getwindow(display, Refnone) < 0)
				error("attach to window");
			draw(screen, screen->r, display->white, nil, ZP);
			iconinit();
			scrlresize();
			rowresize(&row, screen->clipr);
			break;
		case MPlumb:
			if(strcmp(pm->type, "text") == 0){
				act = plumblookup(pm->attr, "action");
				if(act==nil || strcmp(act, "showfile")==0)
					plumblook(pm);
				else if(strcmp(act, "showdata")==0)
					plumbshow(pm);
			}
			plumbfree(pm);
			break;
		case MWarnings:
			break;
		case MMouse:
			/*
			 * Make a copy so decisions are consistent; mousectl changes
			 * underfoot.  Can't just receive into m because this introduces
			 * another race; see /sys/src/libdraw/mouse.c.
			 */
			m = mousectl->m;
			qlock(&row.lk);
			t = rowwhich(&row, m.xy);

			if((t!=mousetext && t!=nil && t->w!=nil) &&
				(mousetext==nil || mousetext->w==nil || t->w->id!=mousetext->w->id)) {
				xfidlog(t->w, "focus");
				if(t->w->body.file->nname > 0){
					char *bname = runetobyte(t->w->body.file->name, t->w->body.file->nname);
					if(bname != nil){
						drawsetlabel(bname);
						free(bname);
					}
				}
			}

			if(t!=mousetext && mousetext!=nil && mousetext->w!=nil){
				winlock(mousetext->w, 'M');
				mousetext->eq0 = ~0;
				wincommit(mousetext->w, mousetext);
				winunlock(mousetext->w);
			}
			mousetext = t;
			if(t == nil)
				goto Continue;
			w = t->w;
			if(t==nil || m.buttons==0)
				goto Continue;
			but = 0;
			if(m.buttons == 1)
				but = 1;
			else if(m.buttons == 2)
				but = 2;
			else if(m.buttons == 4)
				but = 3;
			barttext = t;
			if(t->what==Body && ptinrect(m.xy, t->scrollr)){
				if(but){
					if(swapscrollbuttons){
						if(but == 1)
							but = 3;
						else if(but == 3)
							but = 1;
					}
					winlock(w, 'M');
					t->eq0 = ~0;
					textscroll(t, but);
					winunlock(w);
				}
				goto Continue;
			}
			/* scroll buttons, wheels, etc. */
			if(w != nil && (m.buttons & (8|16))){
				if(m.buttons & 8)
					but = Kscrolloneup;
				else
					but = Kscrollonedown;
				winlock(w, 'M');
				t->eq0 = ~0;
				texttype(t, but);
				winunlock(w);
				goto Continue;
			}
			if(ptinrect(m.xy, t->scrollr)){
				if(but){
					if(t->what == Columntag)
						rowdragcol(&row, t->col, but);
					else if(t->what == Tag){
						coldragwin(t->col, t->w, but);
						if(t->w)
							barttext = &t->w->body;
					}
					if(t->col)
						activecol = t->col;
				}
				goto Continue;
			}
			if(m.buttons){
				if(w)
					winlock(w, 'M');
				t->eq0 = ~0;
				if(w)
					wincommit(w, t);
				else
					textcommit(t, TRUE);
				if(m.buttons & (1|(1<<Shift))){
					if(m.buttons & (1<<Shift))
						textselectextend(t);
					else
						textselect(t);
					if(w)
						winsettag(w);
					argtext = t;
					seltext = t;
					if(t->col)
						activecol = t->col;	/* button 1 only */
					if(t->w!=nil && t==&t->w->body)
						activewin = t->w;
				}else if(m.buttons & 2){
					if(textselect2(t, &q0, &q1, &argt))
						execute(t, q0, q1, FALSE, argt);
				}else if(m.buttons & (4|(4<<Shift))){
					if(textselect3(t, &q0, &q1))
						look3(t, q0, q1, FALSE, (m.buttons&(4<<Shift))!=0);
				}
				if(w)
					winunlock(w);
				goto Continue;
			}
    Continue:
			qunlock(&row.lk);
			break;
		}
	}
}

/*
 * There is a race between process exiting and our finding out it was ever created.
 * This structure keeps a list of processes that have exited we haven't heard of.
 */
typedef struct Pid Pid;
struct Pid
{
	int	pid;
	char	msg[ERRMAX];
	Pid	*next;
};

void
waitthread(void *v)
{
	Waitmsg *w;
	Command *c, *lc;
	uint pid;
	int found, ncmd, themeupdate;
	Rune *cmd;
	char *err;
	Text *t;
	Pid *pids, *p, *lastp;
	enum { WErr, WKill, WWait, WCmd, WTheme, NWALT };
	Alt alts[NWALT+1];

	USED(v);
	threadsetname("waitthread");
	pids = nil;
	alts[WErr].c = cerr;
	alts[WErr].v = &err;
	alts[WErr].op = CHANRCV;
	alts[WKill].c = ckill;
	alts[WKill].v = &cmd;
	alts[WKill].op = CHANRCV;
	alts[WWait].c = cwait;
	alts[WWait].v = &w;
	alts[WWait].op = CHANRCV;
	alts[WCmd].c = ccommand;
	alts[WCmd].v = &c;
	alts[WCmd].op = CHANRCV;
	alts[WTheme].c = ctheme;
	alts[WTheme].v = &themeupdate;
	alts[WTheme].op = CHANRCV;
	alts[NWALT].op = CHANEND;

	command = nil;
	for(;;){
		switch(alt(alts)){
		case WErr:
			qlock(&row.lk);
			warning(nil, "%s", err);
			free(err);
			flushimage(display, 1);
			qunlock(&row.lk);
			break;
		case WKill:
			found = FALSE;
			ncmd = runestrlen(cmd);
			for(c=command; c; c=c->next){
				/* -1 for blank */
				if(runeeq(c->name, c->nname-1, cmd, ncmd) == TRUE){
					if(postnote(PNGROUP, c->pid, "kill") < 0)
						warning(nil, "kill %S: %r\n", cmd);
					found = TRUE;
				}
			}
			if(!found)
				warning(nil, "Kill: no process %S\n", cmd);
			free(cmd);
			break;
		case WTheme:
			qlock(&row.lk);
			iconinit();
			rowupdatecols(&row);
			rowresize(&row, screen->r);
			flushimage(display, 1);
			qunlock(&row.lk);
			break;
		case WWait:
			pid = w->pid;
			lc = nil;
			for(c=command; c; c=c->next){
				if(c->pid == pid){
					if(lc)
						lc->next = c->next;
					else
						command = c->next;
					break;
				}
				lc = c;
			}
			qlock(&row.lk);
			t = &row.tag;
			textcommit(t, TRUE);
			if(c == nil){
				/* helper processes use this exit status */
				if(strncmp(w->msg, "libthread", 9) != 0){
					p = emalloc(sizeof(Pid));
					p->pid = pid;
					strncpy(p->msg, w->msg, sizeof(p->msg));
					p->next = pids;
					pids = p;
				}
			}else{
				if(search(t, c->name, c->nname, FALSE)){
					textdelete(t, t->q0, t->q1, TRUE);
					textsetselect(t, 0, 0);
				}
				if(w->msg[0])
					warning(c->md, "%.*S: exit %s\n", c->nname-1, c->name, w->msg);
				flushimage(display, 1);
			}
			qunlock(&row.lk);
			free(w);
    Freecmd:
			if(c){
				if(c->iseditcmd)
					sendul(cedit, 0);
				free(c->text);
				free(c->name);
				fsysdelid(c->md);
				free(c);
			}
			break;
		case WCmd:
			/* has this command already exited? */
			lastp = nil;
			for(p=pids; p!=nil; p=p->next){
				if(p->pid == c->pid){
					if(p->msg[0])
						warning(c->md, "%s\n", p->msg);
					if(lastp == nil)
						pids = p->next;
					else
						lastp->next = p->next;
					free(p);
					goto Freecmd;
				}
				lastp = p;
			}
			c->next = command;
			command = c;
			qlock(&row.lk);
			t = &row.tag;
			textcommit(t, TRUE);
			textinsert(t, 0, c->name, c->nname, TRUE);
			textsetselect(t, 0, 0);
			flushimage(display, 1);
			qunlock(&row.lk);
			break;
		}
	}
}

void
xfidallocthread(void *v)
{
	Xfid *xfree, *x;
	enum { Alloc, Free, N };
	static Alt alts[N+1];

	USED(v);
	threadsetname("xfidallocthread");
	alts[Alloc].c = cxfidalloc;
	alts[Alloc].v = nil;
	alts[Alloc].op = CHANRCV;
	alts[Free].c = cxfidfree;
	alts[Free].v = &x;
	alts[Free].op = CHANRCV;
	alts[N].op = CHANEND;

	xfree = nil;
	for(;;){
		switch(alt(alts)){
		case Alloc:
			x = xfree;
			if(x)
				xfree = x->next;
			else{
				x = emalloc(sizeof(Xfid));
				x->c = chancreate(sizeof(void(*)(Xfid*)), 0);
				chansetname(x->c, "xc%p", x->c);
				x->arg = x;
				threadcreate(xfidctl, x->arg, STACK);
			}
			sendp(cxfidalloc, x);
			break;
		case Free:
			x->next = xfree;
			xfree = x;
			break;
		}
	}
}

/* this thread, in the main proc, allows fsysproc to get a window made without doing graphics */
void
newwindowthread(void *v)
{
	Window *w;

	USED(v);
	threadsetname("newwindowthread");

	for(;;){
		/* only fsysproc is talking to us, so synchronization is trivial */
		recvp(cnewwindow);
		w = makenewwindow(nil);
		winsettag(w);
		xfidlog(w, "new");
		sendp(cnewwindow, w);
	}
}

Reffont*
rfget(int fix, int save, int setfont, char *name)
{
	Reffont *r;
	Font *f;
	int i;

	r = nil;
	if(name == nil){
		name = fontnames[fix];
		r = reffonts[fix];
	}
	if(r == nil){
		for(i=0; i<nfontcache; i++)
			if(strcmp(name, fontcache[i]->f->name) == 0 ||
			   (fontcache[i]->f->namespec != nil && strcmp(name, fontcache[i]->f->namespec) == 0)){
				r = fontcache[i];
				goto Found;
			}
		f = openfont(display, name);
		if(f == nil){
			warning(nil, "can't open font file %s: %r\n", name);
			return nil;
		}
		r = emalloc(sizeof(Reffont));
		r->f = f;
		fontcache = erealloc(fontcache, (nfontcache+1)*sizeof(Reffont*));
		fontcache[nfontcache++] = r;
	}
    Found:
	if(save){
		incref(&r->ref);
		if(reffonts[fix])
			rfclose(reffonts[fix]);
		reffonts[fix] = r;
		if(name != fontnames[fix]){
			free(fontnames[fix]);
			fontnames[fix] = estrdup(name);
		}
	}
	if(setfont){
		reffont.f = r->f;
		incref(&r->ref);
		rfclose(reffonts[0]);
		font = r->f;
		reffonts[0] = r;
		incref(&r->ref);
		iconinit();
	}
	incref(&r->ref);
	return r;
}

/*
 * Find the last contiguous digit run in s (e.g. "5x10a" -> "10", "euro.8" -> "8").
 * Returns start of digit run in *pdig, end (past last digit) in *pdigend.
 * Returns 0 if no digit run, 1 on success.
 */
static int
plan9lastdigits(char *s, char **pdig, char **pdigend)
{
	char *p, *lastdigit;

	if(s == nil || *s == 0)
		return 0;
	p = s + strlen(s);
	while(p > s && (*(p-1) < '0' || *(p-1) > '9'))
		p--;
	if(p <= s)
		return 0;
	lastdigit = p;	/* p already points past last digit (e.g. to '.' in "euro.8.font") */
	while(p > s && *(p-1) >= '0' && *(p-1) <= '9')
		p--;
	*pdig = p;
	*pdigend = lastdigit;	/* past last digit */
	return 1;
}

/*
 * For Plan 9 bitmap font path that doesn't exist, find nearest available size by
 * listing the directory. Size is the last digit run (e.g. euro.8, 5x10a, 8x13).
 * For names like unicode.8x13.font we use a broad prefix (unicode.) so we consider
 * all unicode.*.font files and pick the next size by height (e.g. 8x13 -> 6x12).
 * Returns path that exists or nil.
 */
static char*
plan9fontnearest(char *desiredpath, int cursize, int delta)
{
	char *dirpath, *filename, *prefix, *suffix, *p, *q, *newpath, *bestname;
	char *desired_var;
	int fd, ndir, i, j, best, cur, nsizes, prefixlen, suffixlen, broadlen, desired_varlen;
	Dir *darr;
	static int sizes[64];
	static char *names[64];

	if(desiredpath == nil)
		return nil;
	p = strrchr(desiredpath, '/');
	if(p == nil)
		return nil;
	dirpath = emalloc(p - desiredpath + 1);
	memmove(dirpath, desiredpath, p - desiredpath);
	dirpath[p - desiredpath] = '\0';
	filename = p + 1;
	if(strstr(filename, ".font") == nil){
		free(dirpath);
		return nil;
	}
	if(!plan9lastdigits(filename, &p, &q)){
		free(dirpath);
		return nil;
	}
	prefixlen = p - filename;
	prefix = emalloc(prefixlen + 1);
	memmove(prefix, filename, prefixlen);
	prefix[prefixlen] = '\0';
	suffix = q;
	suffixlen = strlen(suffix);
	/* Variant: part between size and ".font" (e.g. "" for 8x13, "B" for 8x13B, "O" for 8x13O). Prefer same variant when increasing. */
	desired_var = q;
	desired_varlen = (filename + strlen(filename) - 5) - q;
	/* Broad prefix for listing: unicode.8x -> unicode. so we see 6x12, 8x13, etc. */
	broadlen = prefixlen;
	for(i = prefixlen - 1; i > 0; i--)
		if(prefix[i] == 'x' && i > 0 && prefix[i-1] >= '0' && prefix[i-1] <= '9'){
			broadlen = i - 1;	/* e.g. "unicode.8x" -> before '8' */
			while(broadlen > 0 && prefix[broadlen-1] != '.')
				broadlen--;
			break;
		}
	fd = open(dirpath, OREAD);
	if(fd < 0){
		free(prefix);
		free(dirpath);
		return nil;
	}
	nsizes = 0;
	while((ndir = dirread(fd, (Dir**)&darr)) > 0){
		for(i = 0; i < ndir && nsizes < 64; i++){
			int len = strlen(darr[i].name);
			char *dig, *digend;
			if(len < 5 || strcmp(darr[i].name + len - 5, ".font") != 0)
				continue;
			if(broadlen > 0 && (len < broadlen || strncmp(darr[i].name, prefix, broadlen) != 0))
				continue;
			if(suffixlen > 0 && (len < suffixlen || strcmp(darr[i].name + len - suffixlen, suffix) != 0))
				continue;
			if(plan9lastdigits(darr[i].name, &dig, &digend)){
				cur = atoi(dig);
				if(cur >= 1){
					for(j = 0; j < nsizes && sizes[j] != cur; j++)
						;
					if(j >= nsizes){
						sizes[nsizes] = cur;
						names[nsizes] = estrdup(darr[i].name);
						nsizes++;
					}else{
						/* duplicate size: when increasing prefer same variant (8x13 over 8x13O) then larger width; when decreasing prefer same width (prefix) */
						if(delta > 0){
							int new_varlen, stored_varlen, new_matches, stored_matches;
							char *stordig, *stordigend;

							new_varlen = (darr[i].name + len - 5) - digend;
							new_matches = (new_varlen == desired_varlen && (desired_varlen == 0 || (memcmp(digend, desired_var, desired_varlen) == 0)));
							stored_matches = 0;
							if(plan9lastdigits(names[j], &stordig, &stordigend)){
								stored_varlen = (names[j] + strlen(names[j]) - 5) - stordigend;
								stored_matches = (stored_varlen == desired_varlen && (desired_varlen == 0 || (memcmp(stordigend, desired_var, desired_varlen) == 0)));
							}
							if((new_matches && (!stored_matches || strcmp(darr[i].name, names[j]) > 0)) ||
							   (!new_matches && !stored_matches && strcmp(darr[i].name, names[j]) > 0)){
								free(names[j]);
								names[j] = estrdup(darr[i].name);
							}
						}else if(delta < 0 && prefixlen > 0 && strncmp(darr[i].name, prefix, prefixlen) == 0){
							free(names[j]);
							names[j] = estrdup(darr[i].name);
						}
					}
				}
			}
		}
		free(darr);
	}
	close(fd);
	if(nsizes == 0){
		free(prefix);
		free(dirpath);
		return nil;
	}
	best = -1;
	if(delta > 0){
		for(i = 0; i < nsizes; i++)
			if(sizes[i] > cursize && (best < 0 || sizes[i] < best))
				best = sizes[i];
	}else{
		for(i = 0; i < nsizes; i++)
			if(sizes[i] < cursize && (best < 0 || sizes[i] > best))
				best = sizes[i];
	}
	if(best < 0){
		for(i = 0; i < nsizes; i++)
			free(names[i]);
		free(prefix);
		free(dirpath);
		return nil;
	}
	for(j = 0; j < nsizes && sizes[j] != best; j++)
		;
	bestname = names[j];
	newpath = emalloc(strlen(dirpath) + 1 + strlen(bestname) + 1);
	sprint(newpath, "%s/%s", dirpath, bestname);
	for(i = 0; i < nsizes; i++)
		free(names[i]);
	free(prefix);
	free(dirpath);
	return newpath;
}

/*
 * Return a new font name with size adjusted by delta (+1 or -1).
 * Caller should pass the base font path (use f->lodpi->name so we never see "2*" scale).
 * - Plan 9 bitmap: path ending in .N.font (e.g. .../euro.8.font) -> change N; if that
 *   file doesn't exist, resolve to nearest available size in the same directory.
 * - Mac /mnt/font: path ending in /N/font or /Na/font (e.g. .../Menlo-Regular/14a/font) -> change N.
 * We never add or preserve a scale prefix; result is always a plain path. Caller frees result.
 */
char*
fontnamesize(char *name, int delta)
{
	char *base, *p, *q, *fullbase;
	int n, size, has_a;
	char *out;
	char *part1, *part2, *comma;

	if(name == nil)
		return nil;
	base = name;
	fullbase = nil;
	/* strip leading scale prefix (e.g. "2*") so we only change point size */
	while('0' <= *base && *base <= '9')
		base++;
	if(*base == '*' && base > name)
		base++;
	n = strlen(base);
	if(n == 0)
		return nil;
	/* Mac lodpi,hidpi format: resize each part and return "new1,new2" */
	comma = strchr(base, ',');
	if(comma != nil){
		char *first = smprint("%.*s", (int)(comma - base), base);
		part1 = fontnamesize(first, delta);
		free(first);
		if(part1 == nil)
			return nil;
		part2 = fontnamesize(comma + 1, delta);
		if(part2 == nil){
			free(part1);
			return nil;
		}
		out = smprint("%s,%s", part1, part2);
		free(part1);
		free(part2);
		return out;
	}

	/* Mac: /mnt/font/Name/SIZE/font or .../SIZEa/font — size is last path component before "font" */
	if(strncmp(base, "/mnt/font/", 10) == 0){
		char *num_start;
		int len_before;

		if(n < 5 || strcmp(base + n - 4, "font") != 0)
			return nil;
		p = base + n - 5;	/* slash before "font" */
		while(p > base && *p == '/')
			p--;
		if(p <= base)
			return nil;
		q = p + 1;
		while(p > base && *p != '/')
			p--;
		if(*p == '/')
			p++;
		num_start = p;
		has_a = (q > p && *(q-1) == 'a');
		if(has_a)
			q--;
		if(q <= p)
			return nil;
		size = 0;
		while(p < q && *p >= '0' && *p <= '9')
			size = size*10 + *p++ - '0';
		if(p != q)
			return nil;
		size += delta;
		if(size < 1)
			size = 1;
		len_before = num_start - base;
		out = emalloc((base - name) + n + 16);
		if(base > name)
			memmove(out, name, base - name);
		p = out + (base - name);
		memmove(p, base, len_before);
		p += len_before;
		p += sprint(p, "%d", size);
		if(has_a)
			*p++ = 'a';
		/* suffix is "/font"; when has_a, q points to 'a' so skip it */
		strcpy(p, base + (q - base) + (has_a ? 1 : 0));
		return out;
	}

	/* Plan 9: path ending in "font" (with or without leading dot). Size is last digit run.
	 * Always build new path with ".font" extension so we never produce "...Nfont".
	 * Resolve #9/ to real path so open() and plan9fontnearest can list the directory. */
	if(n >= 4 && strcmp(base + n - 4, "font") == 0){
		int cursize, fd, use_dotfont;
		char *dig, *digend, *alt;

		if(strncmp(base, "#9/", 3) == 0){
			fullbase = unsharp(base);
			if(fullbase != nil){
				base = fullbase;
				n = strlen(base);
			}
		}
		use_dotfont = (n >= 5 && base[n - 5] == '.');	/* ends with ".font" */
		if(!plan9lastdigits(base, &dig, &digend))
			goto plan9out;
		cursize = atoi(dig);
		size = cursize + delta;
		if(size < 1)
			size = 1;
		if(use_dotfont){
			out = emalloc((dig - base) + 16 + (base + n - digend) + 1);
			memmove(out, base, dig - base);
			p = out + (dig - base);
			p += sprint(p, "%d", size);
			memmove(p, digend, (base + n - digend) + 1);	/* suffix ".font" + '\0' */
		}else{
			/* stored name ends with "font" (no dot); build with ".font" */
			out = emalloc((dig - base) + 16 + 6);
			memmove(out, base, dig - base);
			p = out + (dig - base);
			p += sprint(p, "%d.font", size);
		}
		fd = open(out, OREAD);
		if(fd >= 0){
			close(fd);
			/* When increasing, still ask plan9fontnearest so we prefer larger width (e.g. 8x13 over 6x13) when same size exists */
			if(delta > 0){
				alt = plan9fontnearest(out, cursize, delta);
				if(alt != nil){
					free(out);
					if(fullbase != nil) free(fullbase);
					return alt;
				}
			}
			if(fullbase != nil) free(fullbase);
			return out;
		}
		alt = plan9fontnearest(out, cursize, delta);
		if(alt != nil){
			free(out);
			if(fullbase != nil) free(fullbase);
			return alt;
		}
		/* No larger/smaller size in directory. Return nil so global resize can require both fonts to have a next size. */
		free(out);
		if(fullbase != nil) free(fullbase);
		return nil;
	}
plan9out:
	if(fullbase != nil) free(fullbase);

	/* fallback: last digit sequence in string (optional trailing 'a') */
	p = base + n - 1;
	while(p >= base && (*p < '0' || *p > '9'))
		p--;
	if(p < base)
		return nil;
	q = p + 1;
	while(p >= base && *p >= '0' && *p <= '9')
		p--;
	p++;
	has_a = (q < base + n && *q == 'a');
	if(has_a)
		q++;
	{
		char *suffix_start = q;
		size = atoi(p);
		size += delta;
		if(size < 1)
			size = 1;
		out = emalloc((base - name) + n + 16);
		if(base > name)
			memmove(out, name, base - name);
		q = out + (base - name);
		memmove(q, base, p - base);
		q += p - base;
		q += sprint(q, "%d", size);
		if(has_a)
			*q++ = 'a';
		strcpy(q, suffix_start);
		return out;
	}
}

void
resizebodyfont(Window *w, void *arg)
{
	Text *t;
	Reffont *newfont;
	char *newname, *aa;
	int delta, from_global, i;
	Dirlist *dp;

	delta = ((struct resizebodyarg*)arg)->delta;
	from_global = ((struct resizebodyarg*)arg)->from_global;
	t = &w->body;
	/* When from_global: we already updated reffont. Bodies that share &reffont
	 * should get reffonts[0] so they display the new default; do not apply delta again.
	 * When not from_global (e.g. column F+/-): always compute new size via fontnamesize. */
	if(from_global && t->reffont == &reffont){
		newfont = reffonts[0];
		if(newfont == nil)
			return;
		draw(screen, w->r, textcols[BACK], nil, ZP);
		/* do not rfclose(&reffont) */
		incref(&newfont->ref);
		t->reffont = newfont;
		t->fr.font = newfont->f;
		frinittick(&t->fr);
		if(w->isdir){
			t->all.min.x++;
			for(i = 0; i < w->ndl; i++){
				dp = w->dlp[i];
				aa = runetobyte(dp->r, dp->nr);
				dp->wid = stringwidth(newfont->f, aa);
				free(aa);
			}
		}
		textresize(t, t->all, TRUE);
		colgrow(w->col, w, -1);
		return;
	}
	newname = fontnamesize(t->reffont->f->lodpi->name, delta);
	if(newname == nil)
		return;
	newfont = rfget(0, FALSE, FALSE, newname);
	free(newname);
	if(newfont == nil)
		return;
	draw(screen, w->r, textcols[BACK], nil, ZP);
	rfclose(t->reffont);
	t->reffont = newfont;
	t->fr.font = newfont->f;
	frinittick(&t->fr);
	if(w->isdir){
		t->all.min.x++;
		for(i = 0; i < w->ndl; i++){
			dp = w->dlp[i];
			aa = runetobyte(dp->r, dp->nr);
			dp->wid = stringwidth(newfont->f, aa);
			free(aa);
		}
	}
	textresize(t, t->all, TRUE);
	colgrow(w->col, w, -1);
}

static void updatetagfont(Window*, void*);

/*
 * After changing the global tag font (reffont), update every tag's frame to use
 * the new font and redraw: row tag, each column tag, each window tag.
 */
static void
updatetagfonts(void)
{
	int i;
	Column *c;
	Text *t;

	t = &row.tag;
	t->fr.font = reffont.f;
	frinittick(&t->fr);
	textresize(t, t->all, TRUE);
	for(i = 0; i < row.ncol; i++){
		c = row.col[i];
		t = &c->tag;
		t->fr.font = reffont.f;
		frinittick(&t->fr);
		textresize(t, t->all, TRUE);
	}
	allwindows(updatetagfont, nil);
}

static void
updatetagfont(Window *w, void *arg)
{
	Text *t;

	USED(arg);
	t = &w->tag;
	t->fr.font = reffont.f;
	frinittick(&t->fr);
	textresize(t, t->all, TRUE);
}

/* Restore global tag font to f and refresh all tags (e.g. after window F+/- so tag font stays unchanged). */
void
restoretagfont(Font *f)
{
	if(reffont.f != f){
		reffont.f = f;
		updatetagfonts();
	}
}

void
globalfontplus(void)
{
	char *newname0, *newname1;
	Reffont *r;
	int delta = 1;
	int have_two;

	newname0 = fontnamesize(reffont.f->lodpi->name, delta);
	if(newname0 == nil && fontnames[0] != nil)
		newname0 = fontnamesize(fontnames[0], delta);	/* fallback when lodpi name differs */
	have_two = (fontnames[1] != nil && strcmp(fontnames[0], fontnames[1]) != 0);
	newname1 = nil;
	if(have_two)
		newname1 = fontnamesize(fontnames[1], delta);
	/* default must succeed; second font only when non-nil */
	if(newname0 == nil){
		free(newname1);
		return;
	}
	r = rfget(0, TRUE, TRUE, newname0);
	free(newname0);
	if(r != nil)
		rfclose(r);
	if(newname1 != nil){
		r = rfget(1, TRUE, FALSE, newname1);
		free(newname1);
		if(r != nil)
			rfclose(r);
	}
	updatetagfonts();
	rowresize(&row, screen->clipr);
	{ struct resizebodyarg a = { 1, 1 }; allwindows(resizebodyfont, &a); }
}

void
globalfontminus(void)
{
	char *newname0, *newname1;
	Reffont *r;
	int delta = -1;
	int have_two;

	newname0 = fontnamesize(reffont.f->lodpi->name, delta);
	if(newname0 == nil && fontnames[0] != nil)
		newname0 = fontnamesize(fontnames[0], delta);	/* fallback when lodpi name differs */
	have_two = (fontnames[1] != nil && strcmp(fontnames[0], fontnames[1]) != 0);
	newname1 = nil;
	if(have_two)
		newname1 = fontnamesize(fontnames[1], delta);
	/* default must succeed; second font only when non-nil */
	if(newname0 == nil){
		free(newname1);
		return;
	}
	r = rfget(0, TRUE, TRUE, newname0);
	free(newname0);
	if(r != nil)
		rfclose(r);
	if(newname1 != nil){
		r = rfget(1, TRUE, FALSE, newname1);
		free(newname1);
		if(r != nil)
			rfclose(r);
	}
	updatetagfonts();
	rowresize(&row, screen->clipr);
	{ struct resizebodyarg a = { -1, 1 }; allwindows(resizebodyfont, &a); }
}

/*
 * Update both default fonts (variable and fixed) by delta so that Font toggle
 * keeps sizes in sync. Only applies if both fonts can be resized (when we have two).
 */
void
updatebothdefaultfonts(int delta)
{
	char *newname0, *newname1;
	Reffont *r;
	int have_two;

	newname0 = fontnamesize(reffonts[0]->f->lodpi->name, delta);
	have_two = (fontnames[1] != nil && strcmp(fontnames[0], fontnames[1]) != 0);
	newname1 = nil;
	if(have_two)
		newname1 = fontnamesize(fontnames[1], delta);
	if(have_two && (newname0 == nil || newname1 == nil)){
		free(newname0);
		free(newname1);
		return;
	}
	if(newname0 == nil)
		return;
	r = rfget(0, TRUE, TRUE, newname0);
	free(newname0);
	if(r != nil)
		rfclose(r);
	if(newname1 != nil){
		r = rfget(1, TRUE, FALSE, newname1);
		free(newname1);
		if(r != nil)
			rfclose(r);
	}
	updatetagfonts();
	rowresize(&row, screen->clipr);
}

void
rfclose(Reffont *r)
{
	int i;

	if(decref(&r->ref) == 0){
		for(i=0; i<nfontcache; i++)
			if(r == fontcache[i])
				break;
		if(i >= nfontcache)
			warning(nil, "internal error: can't find font in cache\n");
		else{
			nfontcache--;
			memmove(fontcache+i, fontcache+i+1, (nfontcache-i)*sizeof(Reffont*));
		}
		freefont(r->f);
		free(r);
	}
}

Cursor boxcursor = {
	{-7, -7},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F,
	 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E,
	 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x00, 0x00}
};

Cursor2 boxcursor2 = {
	{-15, -15},
	{0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xC0, 0x03, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0x00, 0x00, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x3F, 0xFF, 0xFF, 0xFC,
	 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static void
textupdatecols(Text *t, Image **cols)
{
	int i;
	for(i = 0; i < NCOL; i++)
		t->fr.cols[i] = cols[i];
	frinittick(&t->fr);   /* rebuild the cursor bitmap from new cols */
}

void
rowupdatecols(Row *r)
{
	Column *c;
	Window *w;
	int i, j;

	textupdatecols(&r->tag, tagcols);
	for(i = 0; i < r->ncol; i++){
		c = r->col[i];
		textupdatecols(&c->tag, tagcols);
		for(j = 0; j < c->nw; j++){
			w = c->w[j];
			textupdatecols(&w->tag, tagcols);
			textupdatecols(&w->body, textcols);
		}
	}
}

void
iconinit(void)
{
	Rectangle r;
	Image *tmp;
	int i;
	int changed;

	changed = 0;
	if(curtheme != nil && curtheme != THEME)
		changed = 1;
	curtheme = THEME;

	if(tagcols[BACK] != nil && changed == 1) {
		/* TEXT and HTEXT are aliased — clear the alias before looping */
		tagcols[HTEXT]  = nil;
		textcols[HTEXT] = nil;
		for(i = 0; i < NCOL; i++){
			if(tagcols[i]){
				freeimage(tagcols[i]);
				tagcols[i] = nil;
			}
			if(textcols[i]){
				freeimage(textcols[i]);
				textcols[i] = nil;
			}
		}
		if(but2col){ freeimage(but2col); but2col = nil; }
		if(but3col){ freeimage(but3col); but3col = nil; }
	}

	if(tagcols[BACK] == nil || changed == 1){
		/* Blue */
		tagcols[BACK] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->tagback);
		tagcols[HIGH] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->taghi);
		tagcols[BORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->tagbord);
		tagcols[TEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->tagtext);
		tagcols[HTEXT] = tagcols[TEXT];

		/* Yellow */
		textcols[BACK] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->textback);
		textcols[HIGH] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->texthi);
		textcols[BORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->textbord);
		textcols[TEXT] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->texttext);
		textcols[HTEXT] = textcols[TEXT];
	}

	r = Rect(0, 0, Scrollwid, font->height+1);
	if(changed == 0 && button && eqrect(r, button->r))
		return;

	if(button){
		freeimage(button);
		freeimage(modbutton);
		freeimage(colbutton);
	}

	button = allocimage(display, r, screen->chan, 0, DNofill);
	draw(button, r, tagcols[BACK], nil, r.min);
	border(button, r, ButtonBorder, tagcols[BORD], ZP);

	r = button->r;
	modbutton = allocimage(display, r, screen->chan, 0, DNofill);
	draw(modbutton, r, tagcols[BACK], nil, r.min);
	border(modbutton, r, ButtonBorder, tagcols[BORD], ZP);
	r = insetrect(r, ButtonBorder);
	tmp = allocimage(display, Rect(0,0,1,1), screen->chan, 1, curtheme->modbutton);
	draw(modbutton, r, tmp, nil, ZP);
	freeimage(tmp);

	r = button->r;
	colbutton = allocimage(display, r, screen->chan, 0, curtheme->tagbord);

	but2col = allocimage(display, r, screen->chan, 1, curtheme->but2col);
	but3col = allocimage(display, r, screen->chan, 1, curtheme->but3col);
}

/*
 * /dev/snarf updates when the file is closed, so we must open our own
 * fd here rather than use snarffd
 */

void
acmeputsnarf(void)
{
	int i, n;
	Fmt f;
	char *s;

	if(snarfbuf.nc==0)
		return;

	fmtstrinit(&f);
	for(i=0; i<snarfbuf.nc; i+=n){
		n = snarfbuf.nc-i;
		if(n >= NSnarf)
			n = NSnarf;
		bufread(&snarfbuf, i, snarfrune, n);
		if(fmtprint(&f, "%.*S", n, snarfrune) < 0)
			break;
	}
	s = fmtstrflush(&f);
	if(s && s[0])
		putsnarf(s);
	free(s);
}

void
acmegetsnarf(void)
{
	char *s;
	int nb, nr, nulls, len;
	Rune *r;

	s = getsnarf();
	if(s == nil || s[0]==0){
		free(s);
		return;
	}

	len = strlen(s);
	r = runemalloc(len+1);
	cvttorunes(s, len, r, &nb, &nr, &nulls);
	bufreset(&snarfbuf);
	bufinsert(&snarfbuf, 0, r, nr);
	free(r);
	free(s);
}

int
ismtpt(char *file)
{
	int n;

	if(mtpt == nil)
		return 0;

	/* This is not foolproof, but it will stop a lot of them. */
	n = strlen(mtpt);
	return strncmp(file, mtpt, n) == 0 && ((n > 0 && mtpt[n-1] == '/') || file[n] == '/' || file[n] == 0);
}

int
timefmt(Fmt *f)
{
	Tm *tm;

	tm = localtime(va_arg(f->args, ulong));
	return fmtprint(f, "%04d/%02d/%02d %02d:%02d:%02d",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
}
