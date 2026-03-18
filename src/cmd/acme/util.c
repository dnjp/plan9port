#include <u.h>
#include <libc.h>
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

static	Point		prevmouse;
static	Window	*mousew;

Range
range(int q0, int q1)
{
	Range r;

	r.q0 = q0;
	r.q1 = q1;
	return r;
}

Runestr
runestr(Rune *r, uint n)
{
	Runestr rs;

	rs.r = r;
	rs.nr = n;
	return rs;
}

void
cvttorunes(char *p, int n, Rune *r, int *nb, int *nr, int *nulls)
{
	uchar *q;
	Rune *s;
	int j, w;

	/*
	 * Always guaranteed that n bytes may be interpreted
	 * without worrying about partial runes.  This may mean
	 * reading up to UTFmax-1 more bytes than n; the caller
	 * knows this.  If n is a firm limit, the caller should
	 * set p[n] = 0.
	 */
	q = (uchar*)p;
	s = r;
	for(j=0; j<n; j+=w){
		if(*q < Runeself){
			w = 1;
			*s = *q++;
		}else{
			w = chartorune(s, (char*)q);
			q += w;
		}
		if(*s)
			s++;
		else if(nulls)
			*nulls = TRUE;
	}
	*nb = (char*)q-p;
	*nr = s-r;
}

void
error(char *s)
{
	fprint(2, "acme: %s: %r\n", s);
	threadexitsall(nil);
}

Window*
errorwin1(Rune *dir, int ndir, Rune **incl, int nincl)
{
	Window *w;
	Rune *r;
	int i, n;
	static Rune Lpluserrors[] = { '+', 'E', 'r', 'r', 'o', 'r', 's', 0 };

	r = runemalloc(ndir+8);
	if((n = ndir) != 0){
		runemove(r, dir, ndir);
		r[n++] = L'/';
	}
	runemove(r+n, Lpluserrors, 7);
	n += 7;
	w = lookfile(r, n);
	if(w == nil){
		if(row.ncol == 0)
			if(rowadd(&row, nil, -1) == nil)
				error("can't create column to make error window");
		w = coladd(row.col[row.ncol-1], nil, nil, -1);
		w->filemenu = FALSE;
		winsetname_contract(w, r, n);
		xfidlog(w, "new");
	}
	free(r);
	for(i=nincl; --i>=0; ){
		n = runestrlen(incl[i]);
		r = runemalloc(n);
		runemove(r, incl[i], n);
		winaddincl(w, r, n);
	}
	w->autoindent = globalautoindent;
	return w;
}

/* make new window, if necessary; return with it locked */
Window*
errorwin(Mntdir *md, int owner)
{
	Window *w;

	for(;;){
		if(md == nil)
			w = errorwin1(nil, 0, nil, 0);
		else
			w = errorwin1(md->dir, md->ndir, md->incl, md->nincl);
		winlock(w, owner);
		if(w->col != nil)
			break;
		/* window was deleted too fast */
		winunlock(w);
	}
	return w;
}

/*
 * Incoming window should be locked.
 * It will be unlocked and returned window
 * will be locked in its place.
 */
Window*
errorwinforwin(Window *w)
{
	int i, n, nincl, owner;
	Rune **incl;
	Runestr dir;
	Text *t;

	t = &w->body;
	dir = dirname(t, nil, 0);
	if(dir.nr==1 && dir.r[0]=='.'){	/* sigh */
		free(dir.r);
		dir.r = nil;
		dir.nr = 0;
	}
	incl = nil;
	nincl = w->nincl;
	if(nincl > 0){
		incl = emalloc(nincl*sizeof(Rune*));
		for(i=0; i<nincl; i++){
			n = runestrlen(w->incl[i]);
			incl[i] = runemalloc(n+1);
			runemove(incl[i], w->incl[i], n);
		}
	}
	owner = w->owner;
	winunlock(w);
	for(;;){
		w = errorwin1(dir.r, dir.nr, incl, nincl);
		winlock(w, owner);
		if(w->col != nil)
			break;
		/* window deleted too fast */
		winunlock(w);
	}
	return w;
}

typedef struct Warning Warning;

struct Warning{
	Mntdir *md;
	Buffer buf;
	Warning *next;
};

static Warning *warnings;

static
void
addwarningtext(Mntdir *md, Rune *r, int nr)
{
	Warning *warn;

	for(warn = warnings; warn; warn=warn->next){
		if(warn->md == md){
			bufinsert(&warn->buf, warn->buf.nc, r, nr);
			return;
		}
	}
	warn = emalloc(sizeof(Warning));
	warn->next = warnings;
	warn->md = md;
	if(md)
		fsysincid(md);
	warnings = warn;
	bufinsert(&warn->buf, 0, r, nr);
	nbsendp(cwarn, 0);
}

/* called while row is locked */
void
flushwarnings(void)
{
	Warning *warn, *next;
	Window *w;
	Text *t;
	int owner, nr, q0, n;
	Rune *r;

	for(warn=warnings; warn; warn=next) {
		w = errorwin(warn->md, 'E');
		t = &w->body;
		owner = w->owner;
		if(owner == 0)
			w->owner = 'E';
		wincommit(w, t);
		/*
		 * Most commands don't generate much output. For instance,
		 * Edit ,>cat goes through /dev/cons and is already in blocks
		 * because of the i/o system, but a few can.  Edit ,p will
		 * put the entire result into a single hunk.  So it's worth doing
		 * this in blocks (and putting the text in a buffer in the first
		 * place), to avoid a big memory footprint.
		 */
		r = fbufalloc();
		q0 = t->file->b.nc;
		for(n = 0; n < warn->buf.nc; n += nr){
			nr = warn->buf.nc - n;
			if(nr > RBUFSIZE)
				nr = RBUFSIZE;
			bufread(&warn->buf, n, r, nr);
			textbsinsert(t, t->file->b.nc, r, nr, TRUE, &nr);
		}
		textshow(t, q0, t->file->b.nc, 1);
		free(r);
		winsettag(t->w);
		textscrdraw(t);
		w->owner = owner;
		w->dirty = FALSE;
		winunlock(w);
		bufclose(&warn->buf);
		next = warn->next;
		if(warn->md)
			fsysdelid(warn->md);
		free(warn);
	}
	warnings = nil;
}

void
warning(Mntdir *md, char *s, ...)
{
	Rune *r;
	va_list arg;

	va_start(arg, s);
	r = runevsmprint(s, arg);
	va_end(arg);
	if(r == nil)
		error("runevsmprint failed");
	addwarningtext(md, r, runestrlen(r));
	free(r);
}

int
runeeq(Rune *s1, uint n1, Rune *s2, uint n2)
{
	if(n1 != n2)
		return FALSE;
	if(n1 == 0)
		return TRUE;
	if(s1 == nil || s2 == nil)
		return FALSE;
	return memcmp(s1, s2, n1*sizeof(Rune)) == 0;
}

/* true if s[0..n) contains "/-" (win/awd directory label format) */
int
runehasdirlabel(Rune *s, uint n)
{
	uint i;
	if(s == nil || n < 2)
		return FALSE;
	for(i = 0; i < n-1; i++)
		if(s[i] == '/' && s[i+1] == '-')
			return TRUE;
	return FALSE;
}

uint
min(uint a, uint b)
{
	if(a < b)
		return a;
	return b;
}

uint
max(uint a, uint b)
{
	if(a > b)
		return a;
	return b;
}

char*
runetobyte(Rune *r, int n)
{
	char *s;

	if(r == nil)
		return nil;
	s = emalloc(n*UTFmax+1);
	setmalloctag(s, getcallerpc(&r));
	snprint(s, n*UTFmax+1, "%.*S", n, r);
	return s;
}

Rune*
bytetorune(char *s, int *ip)
{
	Rune *r;
	int nb, nr;

	nb = strlen(s);
	r = runemalloc(nb+1);
	cvttorunes(s, nb, r, &nb, &nr, nil);
	r[nr] = '\0';
	*ip = nr;
	return r;
}

int
isalnum(Rune c)
{
	/*
	 * Hard to get absolutely right.  Use what we know about ASCII
	 * and assume anything above the Latin control characters is
	 * potentially an alphanumeric.
	 */
	if(c <= ' ')
		return FALSE;
	if(0x7F<=c && c<=0xA0)
		return FALSE;
	if(utfrune("!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~", c))
		return FALSE;
	return TRUE;
}

int
rgetc(void *v, uint n)
{
	return ((Rune*)v)[n];
}

int
tgetc(void *a, uint n)
{
	Text *t;

	t = a;
	if(n >= t->file->b.nc)
		return 0;
	return textreadc(t, n);
}

Rune*
skipbl(Rune *r, int n, int *np)
{
	while(n>0 && (*r==' ' || *r=='\t' || *r=='\n')){
		--n;
		r++;
	}
	*np = n;
	return r;
}

Rune*
findbl(Rune *r, int n, int *np)
{
	while(n>0 && *r!=' ' && *r!='\t' && *r!='\n'){
		--n;
		r++;
	}
	*np = n;
	return r;
}

void
savemouse(Window *w)
{
	prevmouse = mouse->xy;
	mousew = w;
}

int
restoremouse(Window *w)
{
	int did;

	did = 0;
	if(mousew!=nil && mousew==w) {
		moveto(mousectl, prevmouse);
		did = 1;
	}
	mousew = nil;
	return did;
}

void
clearmouse()
{
	mousew = nil;
}

char*
estrdup(char *s)
{
	char *t;

	t = strdup(s);
	if(t == nil)
		error("strdup failed");
	setmalloctag(t, getcallerpc(&s));
	return t;
}

void*
emalloc(uint n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		error("malloc failed");
	setmalloctag(p, getcallerpc(&n));
	memset(p, 0, n);
	return p;
}

void*
erealloc(void *p, uint n)
{
	p = realloc(p, n);
	if(p == nil)
		error("realloc failed");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

/*
 * Heuristic city.
 */
Window*
makenewwindow(Text *t)
{
	Column *c;
	Window *w, *bigw, *emptyw;
	Text *emptyb;
	int i, y, el;

	if(activecol)
		c = activecol;
	else if(seltext && seltext->col)
		c = seltext->col;
	else if(t && t->col)
		c = t->col;
	else{
		if(row.ncol==0 && rowadd(&row, nil, -1)==nil)
			error("can't make column");
		c = row.col[row.ncol-1];
	}
	activecol = c;
	if(t==nil || t->w==nil || c->nw==0)
		return coladd(c, nil, nil, -1);

	/* find biggest window and biggest blank spot */
	emptyw = c->w[0];
	bigw = emptyw;
	for(i=1; i<c->nw; i++){
		w = c->w[i];
		/* use >= to choose one near bottom of screen */
		if(w->body.fr.maxlines >= bigw->body.fr.maxlines)
			bigw = w;
		if(w->body.fr.maxlines-w->body.fr.nlines >= emptyw->body.fr.maxlines-emptyw->body.fr.nlines)
			emptyw = w;
	}
	emptyb = &emptyw->body;
	el = emptyb->fr.maxlines-emptyb->fr.nlines;
	/* if empty space is big, use it */
	if(el>15 || (el>3 && el>(bigw->body.fr.maxlines-1)/2))
		y = emptyb->fr.r.min.y+emptyb->fr.nlines*font->height;
	else{
		/* if this window is in column and isn't much smaller, split it */
		if(t->col==c && Dy(t->w->r)>2*Dy(bigw->r)/3)
			bigw = t->w;
		y = (bigw->r.min.y + bigw->r.max.y)/2;
	}
	w = coladd(c, nil, nil, y);
	if(w->body.fr.maxlines < 2)
		colgrow(w->col, w, 1);
	return w;
}

/* contract leading $HOME to ~ in a Rune path.
 * returns a new allocation; caller must free. */
Rune*
contracthome(Rune *name, int n, int *outn)
{
	char *home;
	Rune *homer;
	int homelen;

	home = getenv("HOME");
	if(home == nil){
		*outn = n;
		return runemalloc(n); /* caller will runemove into it */
	}
	homer = runesmprint("%s", home);
	homelen = runestrlen(homer);
	free(homer);

	if(n >= homelen){
		/* check if name starts with $HOME followed by / or end-of-string */
		Rune *htmp = runesmprint("%s", home);
		int match = runestrncmp(name, htmp, homelen) == 0
			&& (n == homelen || (n > homelen && name[homelen] == '/'));
		free(htmp);
		if(match){
			int rest = n - homelen; /* 0 if exact match, else includes leading / */
			Rune *out = runemalloc(1 + rest + 1);
			out[0] = '~';
			if(rest > 0)
				runemove(out+1, name+homelen, rest);
			*outn = 1 + rest;
			return out;
		}
	}
	*outn = n;
	Rune *out = runemalloc(n);
	runemove(out, name, n);
	return out;
}

/* expand leading ~ or $HOME/$home in a Rune path to the real home directory.
 * takes ownership of arg (frees it if expanded); updates *np. */
Rune*
expandhome(Rune *arg, int *np)
{
	char *home;
	Rune *homer, *expanded;
	int homelen, skip, n;

	n = *np;
	if(n <= 0)
		return arg;

	skip = 0;
	if(arg[0] == '~' && (n == 1 || arg[1] == '/'))
		skip = 1;
	else if(n >= 5
		&& arg[0]=='$' && arg[1]=='h' && arg[3]=='m' && arg[4]=='e'
		&& (arg[2]=='o' || arg[2]=='O')
		&& (n == 5 || arg[5] == '/'))
		skip = 5;

	if(skip == 0)
		return arg;

	home = getenv("HOME");
	if(home == nil)
		return arg;

	homer = runesmprint("%s", home);
	homelen = runestrlen(homer);
	expanded = runemalloc(homelen + (n - skip) + 1);
	runemove(expanded, homer, homelen);
	runemove(expanded + homelen, arg + skip, n - skip);
	free(homer);
	free(arg);
	*np = homelen + (n - skip);
	return expanded;
}

/*
 * expandhome_c: expand leading ~ or $HOME/$home in a C string path.
 * Returns a new allocation the caller must free.
 * Used when emitting window names to external consumers (log, index)
 * so they always see absolute paths regardless of internal ~ storage.
 */
char*
expandhome_c(char *name)
{
	char *home;
	int skip, namelen, homelen;
	char *out;

	if(name == nil)
		return estrdup("");

	namelen = strlen(name);
	home = getenv("HOME");
	if(home == nil)
		return estrdup(name);
	homelen = strlen(home);

	skip = 0;
	if(name[0] == '~' && (namelen == 1 || name[1] == '/'))
		skip = 1;
	else if(namelen >= 5
		&& name[0]=='$' && name[1]=='h' && name[3]=='m' && name[4]=='e'
		&& (name[2]=='o' || name[2]=='O')
		&& (namelen == 5 || name[5] == '/'))
		skip = 5;

	if(skip == 0)
		return estrdup(name);

	out = emalloc(homelen + (namelen - skip) + 1);
	memmove(out, home, homelen);
	memmove(out + homelen, name + skip, namelen - skip);
	out[homelen + (namelen - skip)] = '\0';
	return out;
}
