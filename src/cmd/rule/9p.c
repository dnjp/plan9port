#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <ruler.h>
#include "ruler.h"

enum
{
	Stack = 32*1024
};

typedef struct Dirtab Dirtab;
typedef struct Fid Fid;

struct Dirtab
{
	char	*name;
	uchar	type;
	uint	qid;
	uint	perm;
};

struct Fid
{
	int	fid;
	int	busy;
	int	open;
	int	mode;
	Qid	qid;
	Dirtab	*dir;
	long	offset;
	char	*writebuf;	/* query request accumulated */
	long	writecount;
	char	*response;	/* result for read after write */
	long	resplen;
	Fid	*next;
};

enum
{
	Nhash	= 16,
	Qdir	= 0,
	Qquery	= 1,
	Qrules	= 2,
	NQID
};

static Dirtab dir[] =
{
	{ ".",		QTDIR,	Qdir,	0500|DMDIR },
	{ "query",	QTFILE,	Qquery,	0600 },
	{ "rules",	QTFILE,	Qrules,	0600 },
	{ nil,		0,	0,	0 },
};

static int	srvfd;
static uint	rulertime;
static Fid	*fids[Nhash];
static QLock	queue;
static int	messagesize = 8192+IOHDRSZ;

static struct {
	Lock	lk;
	int	ref;
} rulesref;

/* Last query response for write-then-close; next open/read returns it */
static char	*lastquery_resp;
static long	lastquery_resplen;

static void	fsysproc(void*);
static void	fsysrespond(Fcall*, uchar*, char*);
static Fid*	newfid(int);

static Fcall*	fsysflush(Fcall*, uchar*, Fid*);
static Fcall*	fsysversion(Fcall*, uchar*, Fid*);
static Fcall*	fsysauth(Fcall*, uchar*, Fid*);
static Fcall*	fsysattach(Fcall*, uchar*, Fid*);
static Fcall*	fsyswalk(Fcall*, uchar*, Fid*);
static Fcall*	fsysopen(Fcall*, uchar*, Fid*);
static Fcall*	fsyscreate(Fcall*, uchar*, Fid*);
static Fcall*	fsysread(Fcall*, uchar*, Fid*);
static Fcall*	fsyswrite(Fcall*, uchar*, Fid*);
static Fcall*	fsysclunk(Fcall*, uchar*, Fid*);
static Fcall*	fsysremove(Fcall*, uchar*, Fid*);
static Fcall*	fsysstat(Fcall*, uchar*, Fid*);
static Fcall*	fsyswstat(Fcall*, uchar*, Fid*);

static Fcall*	(*fcall[Tmax])(Fcall*, uchar*, Fid*);

static char	Eperm[]		= "permission denied";
static char	Enotdir[]	= "not a directory";
static char	Enoexist[]	= "file does not exist";
static char	Eisdir[]		= "file is a directory";
static char	Einuse[]		= "rules file already open for write";

static void
initfcall(void)
{
	fcall[Tflush] = fsysflush;
	fcall[Tversion] = fsysversion;
	fcall[Tauth] = fsysauth;
	fcall[Tattach] = fsysattach;
	fcall[Twalk] = fsyswalk;
	fcall[Topen] = fsysopen;
	fcall[Tcreate] = fsyscreate;
	fcall[Tread] = fsysread;
	fcall[Twrite] = fsyswrite;
	fcall[Tclunk] = fsysclunk;
	fcall[Tremove] = fsysremove;
	fcall[Tstat] = fsysstat;
	fcall[Twstat] = fsyswstat;
}

static ulong
getclock(void)
{
	return time(0);
}

void
startfsys(int foreground)
{
	int p[2];

	fmtinstall('F', fcallfmt);
	rulertime = getclock();
	if(pipe(p) < 0)
		error("can't create pipe: %r");
	srvfd = p[0];
	if(post9pservice(p[1], "ruler", nil) < 0){
		fprint(2, "ruler: post9pservice: %r\n");
		fprint(2, "ruler: ensure $NAMESPACE is set (e.g. run from 9term or acme) and 9pserve is in your PATH (%s/bin).\n", getenv("PLAN9") ? getenv("PLAN9") : "/usr/local/plan9");
		error("post9pservice ruler: %r");
	}
	close(p[1]);
	if(foreground)
		fsysproc(nil);
	else
		proccreate(fsysproc, nil, Stack);
}

static void
fsysproc(void *v)
{
	int n;
	Fcall *t;
	Fid *f;
	uchar *buf;

	USED(v);
	initfcall();
	t = nil;
	for(;;){
		buf = malloc(messagesize);
		if(buf == nil)
			error("malloc failed: %r");
		n = read9pmsg(srvfd, buf, messagesize);
		if(n <= 0){
			if(n < 0)
				error("i/o error on server channel");
			threadexitsall("unmounted");
		}
		if(t == nil)
			t = emalloc(sizeof(Fcall));
		if(convM2S(buf, n, t) != n){
			error("convert error in convM2S");
		}
		if(fcall[t->type] == nil)
			fsysrespond(t, buf, "bad fcall type");
		else{
			if(t->type == Tversion || t->type == Tauth)
				f = nil;
			else
				f = newfid(t->fid);
			t = (*fcall[t->type])(t, buf, f);
		}
	}
}

static void
fsysrespond(Fcall *t, uchar *buf, char *err)
{
	int n;

	if(err != nil){
		t->type = Rerror;
		t->ename = err;
		/* log real errors; Tauth "not required" is normal and noisy */
		if(strstr(err, "authentication not required") == nil)
			fprint(2, "ruler: error: %s\n", err);
	}else
		t->type++;
	if(buf == nil)
		buf = emalloc(messagesize);
	n = convS2M(t, buf, messagesize);
	if(n < 0){
		error("convert error in convS2M");
	}
	if(write(srvfd, buf, n) != n)
		error("write error in respond");
	free(buf);
}

static Fid*
newfid(int fid)
{
	Fid *f, *ff, **fh;

	qlock(&queue);
	fh = &fids[fid & (Nhash-1)];
	for(f = *fh; f != nil; f = f->next){
		if(f->fid == fid)
			goto Out;
		if(ff == nil && !f->busy)
			ff = f;
	}
	if(ff != nil){
		ff->fid = fid;
		f = ff;
		goto Out;
	}
	f = emalloc(sizeof(*f));
	f->fid = fid;
	f->next = *fh;
	*fh = f;
Out:
	qunlock(&queue);
	return f;
}

static uint
dostat(Dirtab *d, uchar *buf, uint nbuf, uint t)
{
	Dir dir;

	dir.qid.type = d->type;
	dir.qid.path = d->qid;
	dir.qid.vers = 0;
	dir.mode = d->perm;
	dir.length = 0;
	dir.name = d->name;
	dir.uid = user;
	dir.gid = user;
	dir.muid = user;
	dir.atime = t;
	dir.mtime = t;
	return convD2M(&dir, buf, nbuf);
}

/*
 * Parse request buffer (one attribute per line, name=value).
 * Set *query, *client, *event. Id is required but not used for matching.
 * Return 1 if all four present, 0 otherwise.
 */
static int
parsereq(char *buf, long n, char **query, char **client, char **event)
{
	char *p, *end, *eq;
	int haveq, havec, havee, haveid;

	*query = nil;
	*client = nil;
	*event = nil;
	haveq = havec = havee = haveid = 0;
	end = buf + n;
	for(p = buf; p < end; ){
		while(p < end && (*p == '\n' || *p == '\r')) p++;
		if(p >= end) break;
		if(strncmp(p, RulerQuery "=", 6) == 0){
			p += 6;
			eq = p;
			while(eq < end && *eq != '\n' && *eq != '\r') eq++;
			*query = emalloc(eq - p + 1);
			memmove(*query, p, eq - p);
			(*query)[eq - p] = '\0';
			haveq = 1;
			p = eq;
		}else if(strncmp(p, RulerClient "=", 7) == 0){
			p += 7;
			eq = p;
			while(eq < end && *eq != '\n' && *eq != '\r') eq++;
			*client = emalloc(eq - p + 1);
			memmove(*client, p, eq - p);
			(*client)[eq - p] = '\0';
			havec = 1;
			p = eq;
		}else if(strncmp(p, RulerEvent "=", 6) == 0){
			p += 6;
			eq = p;
			while(eq < end && *eq != '\n' && *eq != '\r') eq++;
			*event = emalloc(eq - p + 1);
			memmove(*event, p, eq - p);
			(*event)[eq - p] = '\0';
			havee = 1;
			p = eq;
		}else if(strncmp(p, RulerId "=", 3) == 0){
			haveid = 1;
			while(p < end && *p != '\n' && *p != '\r') p++;
		}else{
			while(p < end && *p != '\n' && *p != '\r') p++;
		}
	}
	return haveq && havec && havee && haveid;
}

static Fcall*
fsysversion(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	if(t->msize < 256){
		fsysrespond(t, buf, "version: message size too small");
		return t;
	}
	if(t->msize < messagesize)
		messagesize = t->msize;
	t->msize = messagesize;
	if(strncmp(t->version, "9P2000", 6) != 0){
		fsysrespond(t, buf, "unrecognized 9P version");
		return t;
	}
	t->version = "9P2000";
	fsysrespond(t, buf, nil);
	return t;
}

static Fcall*
fsysauth(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	fsysrespond(t, buf, "ruler: authentication not required");
	return t;
}

static Fcall*
fsysattach(Fcall *t, uchar *buf, Fid *f)
{
	Fcall out;

	f->busy = 1;
	f->open = 0;
	f->qid.type = QTDIR;
	f->qid.path = Qdir;
	f->qid.vers = 0;
	f->dir = dir;
	memset(&out, 0, sizeof(Fcall));
	out.type = t->type;
	out.tag = t->tag;
	out.fid = f->fid;
	out.qid = f->qid;
	fsysrespond(&out, buf, nil);
	return t;
}

static Fcall*
fsysflush(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	fsysrespond(t, buf, nil);
	return t;
}

static Fcall*
fsyswalk(Fcall *t, uchar *buf, Fid *f)
{
	Fcall out;
	Fid *nf;
	ulong path;
	Dirtab *d, *curdir;
	Qid q;
	int i;
	uchar type;

	if(f->open){
		fsysrespond(t, buf, "clone of an open fid");
		return t;
	}
	nf = nil;
	if(t->fid != t->newfid){
		nf = newfid(t->newfid);
		if(nf->busy){
			fsysrespond(t, buf, "clone to a busy fid");
			return t;
		}
		nf->busy = 1;
		nf->open = 0;
		nf->dir = f->dir;
		nf->qid = f->qid;
		f = nf;
	}
	out.nwqid = 0;
	q = f->qid;
	curdir = f->dir;
	for(i = 0; i < t->nwname; i++){
		if((q.type & QTDIR) == 0){
			fsysrespond(t, buf, Enotdir);
			return t;
		}
		if(strcmp(t->wname[i], "..") == 0){
			type = QTDIR;
			path = Qdir;
			curdir = &dir[0];
			goto Accept;
		}
		if(strcmp(t->wname[i], ".") == 0){
			type = q.type;
			path = q.path;
			goto Accept;
		}
		d = curdir;
		d++;
		for(; d->name != nil; d++)
			if(strcmp(t->wname[i], d->name) == 0){
				type = d->type;
				path = d->qid;
				curdir = d;
				goto Accept;
			}
		fsysrespond(t, buf, Enoexist);
		return t;
	Accept:
		q.type = type;
		q.vers = 0;
		q.path = path;
		out.wqid[out.nwqid++] = q;
	}
	out.type = t->type;
	out.tag = t->tag;
	if(out.nwqid == t->nwname){
		f->qid = q;
		f->dir = curdir;
	}
	fsysrespond(&out, buf, nil);
	return t;
}

static Fcall*
fsysopen(Fcall *t, uchar *buf, Fid *f)
{
	int m, mode;

	mode = t->mode & ~(OTRUNC|OCEXEC);
	if(mode == OEXEC || (mode&ORCLOSE)){
		fsysrespond(t, buf, Eperm);
		return t;
	}
	switch(mode){
	default:
		fsysrespond(t, buf, Eperm);
		return t;
	case OREAD:
		m = 0400;
		break;
	case OWRITE:
		m = 0200;
		break;
	case ORDWR:
		m = 0600;
		break;
	}
	if(((f->dir->perm & ~(DMDIR|DMAPPEND)) & m) != m){
		fsysrespond(t, buf, Eperm);
		return t;
	}
	if(f->qid.path == Qrules && (mode == OWRITE || mode == ORDWR)){
		lock(&rulesref.lk);
		if(rulesref.ref++ != 0){
			rulesref.ref--;
			unlock(&rulesref.lk);
			fsysrespond(t, buf, Einuse);
			return t;
		}
		unlock(&rulesref.lk);
	}
	if(f->qid.path == Qquery){
		f->writebuf = nil;
		f->writecount = 0;
		f->offset = 0;
		if(mode == OREAD || mode == ORDWR){
			/* Next reader gets last response from write-then-close */
			if(lastquery_resp != nil){
				f->response = lastquery_resp;
				f->resplen = lastquery_resplen;
				lastquery_resp = nil;
				lastquery_resplen = 0;
			}else{
				f->response = nil;
				f->resplen = 0;
			}
		}else{
			f->response = nil;
			f->resplen = 0;
		}
	}
	if(f->qid.path == Qrules && (t->mode & OTRUNC))
		writerules(nil, 0);
	t->qid = f->qid;
	t->iounit = 0;
	qlock(&queue);
	f->mode = mode;
	f->open = 1;
	f->offset = 0;
	qunlock(&queue);
	fsysrespond(t, buf, nil);
	return t;
}

static Fcall*
fsyscreate(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	fsysrespond(t, buf, Eperm);
	return t;
}

static Fcall*
fsysread(Fcall *t, uchar *buf, Fid *f)
{
	uchar *b;
	int i, n, o, e;
	uint len;
	Dirtab *d;
	char *query, *client, *event;
	char *resp;

	if(f->qid.path == Qquery){
		/* If they wrote a request, parse and match now */
		if(f->writebuf != nil && f->writecount > 0){
			if(parsereq(f->writebuf, f->writecount, &query, &client, &event)){
				resp = matchquery(query, client, event);
				free(query);
				free(client);
				free(event);
				if(resp != nil){
					f->response = resp;
					f->resplen = strlen(resp);
				}
			}
			free(f->writebuf);
			f->writebuf = nil;
			f->writecount = 0;
		}
		if(f->response == nil){
			t->count = 0;
			fsysrespond(t, buf, nil);
			return t;
		}
		o = t->offset;
		e = t->offset + t->count;
		if(o >= f->resplen){
			t->count = 0;
			fsysrespond(t, buf, nil);
			return t;
		}
		if(e > f->resplen)
			e = f->resplen;
		n = e - o;
		t->data = emalloc(n);
		memmove(t->data, f->response + o, n);
		t->count = n;
		fsysrespond(t, buf, nil);
		free(t->data);
		t->data = nil;
		return t;
	}
	if(f->qid.path == Qrules){
		char *p;
		p = printrules();
		n = strlen(p);
		t->data = p;
		if(t->offset >= n)
			t->count = 0;
		else{
			t->data = p + t->offset;
			if(t->offset + t->count > n)
				t->count = n - t->offset;
		}
		fsysrespond(t, buf, nil);
		free(p);
		return t;
	}
	if(f->qid.path != Qdir){
		fsysrespond(t, buf, "internal error");
		return t;
	}
	o = t->offset;
	e = t->offset + t->count;
	b = malloc(messagesize - IOHDRSZ);
	if(b == nil){
		fsysrespond(t, buf, "malloc failed");
		return t;
	}
	n = 0;
	len = 0;
	d = dir;
	d++;
	for(i = 0; d->name != nil && i < e; i += len){
		len = dostat(d, b + n, messagesize - IOHDRSZ - n, rulertime);
		if(len <= BIT16SZ)
			break;
		if(i >= o)
			n += len;
		d++;
	}
	t->data = (char*)b;
	t->count = n;
	fsysrespond(t, buf, nil);
	free(b);
	return t;
}

static Fcall*
fsyswrite(Fcall *t, uchar *buf, Fid *f)
{
	char *err;

	switch((int)f->qid.path){
	case Qdir:
		fsysrespond(t, buf, Eisdir);
		return t;
	case Qrules:
		rulertime = getclock();
		err = writerules(t->data, t->count);
		fsysrespond(t, buf, err);
		return t;
	case Qquery:
		if(f->writebuf == nil){
			f->writebuf = emalloc(t->count + 1);
			memmove(f->writebuf, t->data, t->count);
			f->writebuf[t->count] = '\0';
			f->writecount = t->count;
		}else{
			f->writebuf = erealloc(f->writebuf, f->writecount + t->count + 1);
			memmove(f->writebuf + f->writecount, t->data, t->count);
			f->writecount += t->count;
			f->writebuf[f->writecount] = '\0';
		}
		fsysrespond(t, buf, nil);
		return t;
	}
	fsysrespond(t, buf, "internal error");
	return t;
}

static Fcall*
fsysstat(Fcall *t, uchar *buf, Fid *f)
{
	t->stat = emalloc(messagesize - IOHDRSZ);
	t->nstat = dostat(f->dir, t->stat, messagesize - IOHDRSZ, rulertime);
	fsysrespond(t, buf, nil);
	free(t->stat);
	t->stat = nil;
	return t;
}

static Fcall*
fsyswstat(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	fsysrespond(t, buf, Eperm);
	return t;
}

static Fcall*
fsysremove(Fcall *t, uchar *buf, Fid *fid)
{
	USED(fid);
	fsysrespond(t, buf, Eperm);
	return t;
}

static Fcall*
fsysclunk(Fcall *t, uchar *buf, Fid *f)
{
	qlock(&queue);
	if(f->open){
		if(f->qid.path == Qrules && (f->mode == OWRITE || f->mode == ORDWR)){
			writerules(nil, 0);
			lock(&rulesref.lk);
			rulesref.ref--;
			unlock(&rulesref.lk);
		}
		if(f->qid.path == Qquery){
			/* Write but no read: run match and store for next open/read */
			if(f->writebuf != nil && f->writecount > 0 && f->response == nil){
				char *query, *client, *event;
				char *resp;
				if(parsereq(f->writebuf, f->writecount, &query, &client, &event)){
					resp = matchquery(query, client, event);
					free(query);
					free(client);
					free(event);
					if(resp != nil){
						if(lastquery_resp != nil)
							free(lastquery_resp);
						lastquery_resp = resp;
						lastquery_resplen = strlen(resp);
					}
				}
			}
			if(f->writebuf != nil)
				free(f->writebuf);
			if(f->response != nil)
				free(f->response);
		}
	}
	f->busy = 0;
	f->open = 0;
	f->offset = 0;
	f->writebuf = nil;
	f->writecount = 0;
	f->response = nil;
	f->resplen = 0;
	qunlock(&queue);
	fsysrespond(t, buf, nil);
	return t;
}
