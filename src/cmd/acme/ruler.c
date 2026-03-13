/*
 * Ruler client for Acme: query ruler service for directives when a window
 * is associated with a file; apply comfmt, tabstop, tabexpand.
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
	fprint(2, "ruler: %s", buf);
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

void
rulerapply(Window *w)
{
	char *path, *abspath, *querypath;
	int pathlen;

	if(getenv("rulerdebug") != nil)
		rulerdebug = 1;
	if(w == nil || w->body.file == nil || w->body.file->nname == 0){
		if(rulerdebug)
			rulerlog("skip: no file name (win %p)", w);
		return;
	}
	if(w->isdir){
		if(rulerdebug)
			rulerlog("skip: window is directory");
		return;
	}
	path = runetobyte(w->body.file->name, w->body.file->nname);
	if(path == nil){
		if(rulerdebug)
			rulerlog("skip: runetobyte failed");
		return;
	}
	pathlen = strlen(path);
	if(pathlen == 0){
		free(path);
		if(rulerdebug)
			rulerlog("skip: empty path");
		return;
	}
	if(path[0] != '/'){
		abspath = emalloc(pathlen + strlen(wdir) + 2);
		snprint(abspath, pathlen + strlen(wdir) + 2, "%s/%s", wdir, path);
		cleanname(abspath);
		querypath = abspath;
		free(path);
	}else{
		abspath = nil;
		querypath = path;
	}
	{
		CFsys *fs;
		CFid *fid;
		char req[Reqsize];
		char resp[Respsize];
		long n, nw, nr;
		char *line, *end;

		n = snprint(req, sizeof req, "%s=%s\n%s=acme\n%s=open\n%s=%d\n",
			RulerQuery, querypath,
			RulerClient, RulerEvent, RulerId, w->id);
		if(abspath != nil)
			free(abspath);
		else
			free(path);
		if(n >= (int)sizeof req){
			if(rulerdebug)
				rulerlog("skip: request too long");
			return;
		}
		fs = nsmount("ruler", nil);
		if(fs == nil){
			if(rulerdebug)
				rulerlog("nsmount ruler: %r");
			return;
		}
		fid = fsopen(fs, RulerQueryFile, ORDWR);
		if(fid == nil){
			if(rulerdebug)
				rulerlog("fsopen query: %r");
			fsunmount(fs);
			return;
		}
		rulerlog("query win id=%d request %ld bytes", w->id, n);
		if(rulerdebug)
			for(line = req; line < req + n; line = end + 1){
				end = memchr(line, '\n', req + n - line);
				if(end == nil)
					end = req + n;
				if(end > line){
					*end = '\0';
					rulerlog("  %s", line);
					*end = '\n';
				}
			}
		nw = fswrite(fid, req, n);
		if(nw != n){
			if(rulerdebug)
				rulerlog("fswrite: %r (wrote %ld/%ld)", nw, n);
			fsclose(fid);
			fsunmount(fs);
			return;
		}
		fsseek(fid, 0, 0);
		nr = 0;
		while((n = fsread(fid, resp + nr, sizeof resp - nr - 1)) > 0){
			nr += n;
			if(nr >= (long)sizeof resp - 1)
				break;
		}
		fsclose(fid);
		fsunmount(fs);
		if(nr <= 0){
			if(rulerdebug)
				rulerlog("response 0 bytes (no rule matched? check query path and rulerfile patterns)");
			return;
		}
		resp[nr] = '\0';
		if(rulerdebug){
			char *nl = memchr(resp, '\n', nr);
			if(nl != nil)
				*nl = '\0';
			rulerlog("response %ld bytes: %s", nr, resp);
			if(nl != nil)
				*nl = '\n';
		}
		applyresponse(w, resp, nr);
	}
}
