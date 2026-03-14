/*
 * Ruler client for Acme: query ruler service for directives when a window
 * is associated with a file; apply comfmt, tabstop, tabexpand, font.
 * The request includes a "type" field: "win" for win(1) terminal windows
 * (name contains "/-"), "file" for ordinary file windows.
 * See include/ruler.h.
 */
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
#include <9pclient.h>
#include <ruler.h>
#include "dat.h"
#include "fns.h"

enum { Reqsize = 512, Respsize = 4096 };

static int	rulerdebug;

static void
rulerlog(char *fmt, ...)
{
	va_list a;
	char buf[512];

	if(!rulerdebug)
		return;
	va_start(a, fmt);
	vseprint(buf, buf+sizeof buf, fmt, a);
	va_end(a);
	fprint(2, "acme/ruler: %s", buf);
	if(buf[0] != '\0' && buf[strlen(buf)-1] != '\n')
		fprint(2, "\n");
}

/* Copy value into buf; if value is quoted (starts with '), unescape '' -> '. Return buf. */
static char*
parsevalue(char *val, char *buf, int bufsz)
{
	char *p;
	int n;

	if(val == nil || *val == '\0'){
		buf[0] = '\0';
		return buf;
	}
	if(val[0] == '\''){
		val++;
		p = buf;
		n = bufsz - 1;
		while(n > 0 && *val != '\0'){
			if(val[0] == '\'' && val[1] == '\''){
				*p++ = '\'';
				val += 2;
				n--;
			}else if(*val == '\''){
				val++;
				break;
			}else{
				*p++ = *val++;
				n--;
			}
		}
		*p = '\0';
		return buf;
	}
	strncpy(buf, val, bufsz - 1);
	buf[bufsz - 1] = '\0';
	return buf;
}

static void
applydirective(Window *w, char *key, char *val)
{
	char buf[256];
	int n, i;
	Rune rbuf[NCOMFMT];

	if(key == nil || val == nil)
		return;
	if(strcmp(key, "comfmt") == 0){
		int nb, nr;
		parsevalue(val, buf, sizeof buf);
		nr = 0;
		cvttorunes(buf, strlen(buf), rbuf, &nb, &nr, nil);
		n = nr;
		if(n > 0){
			if(n > NCOMFMT)
				n = NCOMFMT;
			w->body.ncomfmt = n;
			runemove(w->body.comfmt, rbuf, n);
			for(i = n; i < NCOMFMT; i++)
				w->body.comfmt[i] = 0;
			rulerlog("apply comfmt=%s", buf);
		}
		return;
	}
	if(strcmp(key, "tabstop") == 0){
		parsevalue(val, buf, sizeof buf);
		n = atoi(buf);
		if(n > 0 && w->body.tabstop != n){
			w->body.tabstop = n;
			winresize(w, w->r, FALSE, TRUE);
			rulerlog("apply tabstop=%d", n);
		}
		return;
	}
	if(strcmp(key, "tabexpand") == 0){
		parsevalue(val, buf, sizeof buf);
		w->body.tabexpand = (strcmp(buf, "on") == 0 || strcmp(buf, "ON") == 0);
		rulerlog("apply tabexpand=%s", w->body.tabexpand ? "on" : "off");
		return;
	}
	if(strcmp(key, "indent") == 0){
		parsevalue(val, buf, sizeof buf);
		w->autoindent = (strcmp(buf, "on") == 0 || strcmp(buf, "ON") == 0);
		rulerlog("apply indent=%s", w->autoindent ? "on" : "off");
		return;
	}
	if(strcmp(key, "font") == 0){
		Reffont *rf;
		int fix;

		parsevalue(val, buf, sizeof buf);
		fix = (strcmp(buf, "fix") == 0);
		rulerlog("font directive: val=%s fix=%d fontnames[0]=%s fontnames[1]=%s",
			buf, fix,
			fontnames[0] ? fontnames[0] : "(nil)",
			fontnames[1] ? fontnames[1] : "(nil)");
		rf = rfget(fix, FALSE, FALSE, nil);
		if(rf != nil){
			rulerlog("apply font=%s -> %s", buf, rf->f->name);
			if(w->body.fr.font == rf->f){
				/* font already set by rulerprefont; no redraw needed */
				rfclose(rf);
				rulerlog("font already set, skipping winresize");
				return;
			}
			rfclose(w->body.reffont);
			w->body.reffont = rf;
			w->body.fr.font = rf->f;
			frinittick(&w->body.fr);
			winresize(w, w->r, FALSE, TRUE);
		} else {
			rulerlog("rfget failed for font=%s (fix=%d)", buf, fix);
		}
		return;
	}
	rulerlog("ignore unknown key=%s", key);
}

/*
 * Parse response body (newline-separated key=value lines); first '=' on a line
 * separates key from value. Apply each known directive to w.
 */
static void
applyresponse(Window *w, char *body, long nbody)
{
	char *p, *eq, *lineend;
	char key[64], val[256];
	int keylen, napplied;

	if(body == nil || nbody <= 0)
		return;
	napplied = 0;
	p = body;
	while(p < body + nbody){
		lineend = memchr(p, '\n', body + nbody - p);
		if(lineend == nil)
			lineend = body + nbody;
		eq = memchr(p, '=', lineend - p);
		if(eq != nil && eq > p){
			keylen = eq - p;
			if(keylen >= sizeof key)
				keylen = sizeof key - 1;
			memmove(key, p, keylen);
			key[keylen] = '\0';
			/* value = rest of line after '=' */
			p = eq + 1;
			if(lineend - p >= (int)sizeof val)
				memmove(val, p, sizeof val - 1), val[sizeof val - 1] = '\0';
			else{
				memmove(val, p, lineend - p);
				val[lineend - p] = '\0';
			}
			applydirective(w, key, val);
			napplied++;
		}
		p = lineend + 1;
	}
	if(rulerdebug && napplied == 0)
		rulerlog("response had no key=value lines (body %ld bytes)", nbody);
}

/*
 * rulerwintype returns the logical window type for use in ruler queries.
 * It checks w->dumpstr first: win(1) sets this to the command used to
 * recreate the window (e.g. "win rc"), so any dumpstr starting with "win"
 * reliably identifies a terminal window. Falls back to detecting the "/-"
 * path pattern that win(1) writes into the window name.
 */
static char*
rulerwintype(Window *w)
{
	char *path, *t;

	if(w == nil)
		return "file";

	/* Primary: dumpstr is set by win(1) via the "dump" ctl message. */
	if(w->dumpstr != nil && strncmp(w->dumpstr, "win", 3) == 0 &&
	   (w->dumpstr[3] == '\0' || w->dumpstr[3] == ' ' || w->dumpstr[3] == '\n'))
		return "win";

	/* Fallback: win(1) names its window "<dir>/-<shell>". */
	if(w->body.file == nil || w->body.file->nname == 0)
		return "file";
	path = runetobyte(w->body.file->name, w->body.file->nname);
	if(path == nil)
		return "file";
	t = strstr(path, "/-") != nil ? "win" : "file";
	free(path);
	return t;
}

/*
 * rulerquerypath builds the absolute query path for w's file name.
 * Returns an allocated string the caller must free, or nil on failure.
 */
static char*
rulerquerypath(Window *w)
{
	char *path, *abspath;
	int pathlen;

	path = runetobyte(w->body.file->name, w->body.file->nname);
	if(path == nil || path[0] == '\0'){
		free(path);
		return nil;
	}
	if(path[0] != '/'){
		pathlen = strlen(path);
		abspath = emalloc(pathlen + strlen(wdir) + 2);
		snprint(abspath, pathlen + strlen(wdir) + 2, "%s/%s", wdir, path);
		cleanname(abspath);
		free(path);
		return abspath;
	}
	return path;
}

/*
 * rulerquery sends a ruler query for w and returns the response body in an
 * allocated buffer (caller must free). Returns nil if ruler is unavailable or
 * no match. querypath is consumed (freed) by this function.
 */
static char*
rulerquery(Window *w, char *querypath)
{
	CFsys *fs;
	CFid *fid;
	char req[Reqsize];
	char *resp;
	long n, nw, nr;
	char *wtype;

	wtype = rulerwintype(w);
	n = snprint(req, sizeof req, "%s=%s\n%s=acme\n%s=open\n%s=%d\n%s=%s\n",
		RulerQuery, querypath,
		RulerClient, RulerEvent, RulerId, w->id,
		RulerType, wtype);
	free(querypath);
	if(n >= (int)sizeof req)
		return nil;
	fs = nsmount("ruler", nil);
	if(fs == nil)
		return nil;
	fid = fsopen(fs, RulerQueryFile, ORDWR);
	if(fid == nil){
		fsunmount(fs);
		return nil;
	}
	nw = fswrite(fid, req, n);
	if(nw != n){
		fsclose(fid);
		fsunmount(fs);
		return nil;
	}
	fsseek(fid, 0, 0);
	resp = emalloc(Respsize);
	nr = 0;
	while((n = fsread(fid, resp + nr, Respsize - nr - 1)) > 0){
		nr += n;
		if(nr >= Respsize - 1)
			break;
	}
	fsclose(fid);
	fsunmount(fs);
	if(nr <= 0){
		free(resp);
		return nil;
	}
	resp[nr] = '\0';
	return resp;
}

/*
 * rulerprefont queries the ruler for w's font directive and applies it
 * directly to w->body without triggering a winresize. Call this after
 * winsetname but before textload so the frame is filled with the correct
 * font from the start, avoiding a double render.
 */
void
rulerprefont(Window *w)
{
	char *querypath, *resp, *p, *eq, *lineend;
	char key[64], val[256], buf[256];
	int fix;
	Reffont *rf;
	if(getenv("rulerdebug") != nil)
		rulerdebug = 1;
	if(w == nil || w->body.file == nil || w->body.file->nname == 0)
		return;
	if(w->isdir || w->isscratch)
		return;
	querypath = rulerquerypath(w);
	if(querypath == nil)
		return;

	resp = rulerquery(w, querypath); /* querypath is freed by rulerquery */
	if(resp == nil)
		return;

	/* scan response for font= directive only */
	p = resp;
	while(*p != '\0'){
		lineend = strchr(p, '\n');
		if(lineend == nil)
			lineend = p + strlen(p);
		eq = memchr(p, '=', lineend - p);
		if(eq != nil && eq > p){
			int keylen = eq - p;
			if(keylen >= (int)sizeof key)
				keylen = sizeof key - 1;
			memmove(key, p, keylen);
			key[keylen] = '\0';
			if(strcmp(key, "font") == 0){
				int vlen = lineend - (eq + 1);
				if(vlen >= (int)sizeof val)
					vlen = sizeof val - 1;
				memmove(val, eq + 1, vlen);
				val[vlen] = '\0';
				parsevalue(val, buf, sizeof buf);
				fix = (strcmp(buf, "fix") == 0);
				rf = rfget(fix, FALSE, FALSE, nil);
				if(rf != nil){
					rulerlog("prefont: apply font=%s -> %s", buf, rf->f->name);
					rfclose(w->body.reffont);
					w->body.reffont = rf;
					w->body.fr.font = rf->f;
					w->body.fr.maxtab = w->body.tabstop * stringwidth(rf->f, "0");
					frsetrects(&w->body.fr, w->body.fr.entire, w->body.fr.b);
					frinittick(&w->body.fr);
				}
				break;
			}
		}
		p = (*lineend == '\0') ? lineend : lineend + 1;
	}
	free(resp);
}

void
rulerapply(Window *w)
{
	char *querypath, *resp;

	if(getenv("rulerdebug") != nil)
		rulerdebug = 1;
	if(w == nil || w->body.file == nil || w->body.file->nname == 0){
		rulerlog("skip: no file name");
		return;
	}
	if(w->isdir){
		rulerlog("skip: directory");
		return;
	}
	if(w->isscratch){
		rulerlog("skip: scratch");
		return;
	}
	querypath = rulerquerypath(w);
	if(querypath == nil){
		rulerlog("skip: empty path");
		return;
	}
	rulerlog("query id=%d type=%s path=%s", w->id, rulerwintype(w), querypath);
	resp = rulerquery(w, querypath); /* querypath freed by rulerquery */
	if(resp == nil){
		rulerlog("no match");
		return;
	}
	if(rulerdebug){
		char *nl = memchr(resp, '\n', strlen(resp));
		if(nl != nil) *nl = '\0';
		rulerlog("response: %s", resp);
		if(nl != nil) *nl = '\n';
	}
	applyresponse(w, resp, strlen(resp));
	free(resp);
}
