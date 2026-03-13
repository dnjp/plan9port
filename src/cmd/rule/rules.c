#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <ctype.h>
#include <regexp.h>
#include "ruler.h"

typedef struct Input Input;

struct Input
{
	char	*file;
	Biobuf	*fd;
	uchar	*s;
	uchar	*end;
	int	lineno;
	Input	*next;
};

static int	parsing;
static Input	*input;
static int	maxincl = 10;

Rulerule	**rules;
char		*rulerfile;
char		*user;
char		*home;
char		*lasterror;

static void
printinputstackrev(Input *in)
{
	if(in == nil)
		return;
	printinputstackrev(in->next);
	fprint(2, "%s:%d: ", in->file, in->lineno);
}

void
printinputstack(void)
{
	printinputstackrev(input);
}

static void
pushinput(char *name, int fd, uchar *str)
{
	Input *in;
	int depth;

	depth = 0;
	for(in = input; in != nil; in = in->next)
		if(depth++ >= maxincl)
			parseerror("include stack too deep; max %d", maxincl);

	in = emalloc(sizeof(Input));
	in->file = estrdup(name);
	in->next = input;
	input = in;
	if(str != nil){
		in->s = str;
		in->end = nil;
	}else{
		in->fd = emalloc(sizeof(Biobuf));
		if(Binit(in->fd, fd, OREAD) < 0)
			parseerror("can't init Bio for rules file: %r");
		in->s = nil;
	}
}

int
popinput(void)
{
	Input *in;

	in = input;
	if(in == nil)
		return 0;
	input = in->next;
	if(in->fd != nil){
		Bterm(in->fd);
		free(in->fd);
	}
	free(in->file);
	free(in);
	return 1;
}

static int
getc(void)
{
	if(input == nil)
		return -1;
	if(input->fd != nil)
		return Bgetc(input->fd);
	if(input->s != nil && input->end != nil && input->s < input->end)
		return *(input->s)++;
	if(input->s != nil && input->end == nil && *input->s != '\0')
		return *(input->s)++;
	return -1;
}

char*
getline(void)
{
	static int n;
	static char *s;
	int c, i;

	i = 0;
	for(;;){
		c = getc();
		if(c < 0){
			/* EOF on current input: pop to parent and keep reading */
			if(popinput())
				continue;
			/* no more input */
			if(i == 0)
				return nil;
			break;
		}
		if(i == n){
			n += 256;
			s = erealloc(s, n);
		}
		if(c == '\0' || c == '\n')
			break;
		s[i++] = c;
	}
	s[i] = '\0';
	return s;
}

static int
doinclude(char *t)
{
	char buf[256];
	int fd;

	fd = open(t, OREAD);
	if(fd < 0 && t[0] != '/' && strncmp(t, "./", 2) != 0 && strncmp(t, "../", 3) != 0){
		/* check $PLAN9/rule/ first (where initial.rules lives) */
		snprint(buf, sizeof buf, "#9/rule/%s", t);
		fd = open(unsharp(buf), OREAD);
	}
	if(fd < 0 && t[0] != '/' && strncmp(t, "./", 2) != 0 && strncmp(t, "../", 3) != 0){
		/* also check $PLAN9/ruler/ for user-installed rule libraries */
		snprint(buf, sizeof buf, "#9/ruler/%s", t);
		fd = open(unsharp(buf), OREAD);
	}
	if(fd < 0 && home != nil && t[0] != '/' && strncmp(t, "./", 2) != 0 && strncmp(t, "../", 3) != 0){
		snprint(buf, sizeof buf, "%s/lib/ruler/%s", home, t);
		fd = open(buf, OREAD);
	}
	if(fd < 0)
		parseerror("can't open %s for inclusion", t);
	pushinput(t, fd, nil);
	return 1;
}

static int
include(char *s)
{
	char *args[4];
	int n;

	if(strncmp(s, "include", 7) != 0)
		return 0;
	n = tokenize(s, args, nelem(args));
	if(n < 2)
		parseerror("malformed include");
	if(strcmp(args[0], "include") != 0)
		return 0;
	if(args[1][0] == '#')
		parseerror("malformed include");
	return doinclude(args[1]);
}

static Pathpat*
addpat(Pathpat *head, int type, char *value, void *regex)
{
	Pathpat *p;

	p = emalloc(sizeof(Pathpat));
	p->type = type;
	p->value = value ? estrdup(value) : nil;
	p->regex = regex;
	p->next = head;
	return p;
}

static void
freepat(Pathpat *p)
{
	Pathpat *n;

	while(p != nil){
		n = p->next;
		free(p->value);
		if(p->regex != nil)
			free(p->regex);
		free(p);
		p = n;
	}
}

void
freerule(Rulerule *r)
{
	if(r == nil)
		return;
	freepat(r->pats);
	free(r->client);
	free(r->event);
	free(r->directives);
	free(r);
}

void
freerules(Rulerule **r)
{
	int i;

	if(r == nil)
		return;
	for(i = 0; r[i] != nil; i++)
		freerule(r[i]);
	free(r);
}

/*
 * Parse a pattern from "query matches 'pat'" (pat may be suffix, prefix, or regex).
 * Returns new Pathpat (prepended to head) or head on parse error (and sets lasterror).
 */
static Pathpat*
parse_query_matches(char *pat, Pathpat *head)
{
	Reprog *re;

	if(pat == nil || *pat == '\0')
		return head;
	/* Suffix: *suffix (e.g. *.go, *Makefile) — always literal suffix, never regex */
	if(pat[0] == '*' && pat[1] != '\0')
		return addpat(head, Patsuffix, pat+1, nil);
	/* Prefix: /path or $home/path */
	if(pat[0] == '/' || (strncmp(pat, "$home", 5) == 0 && (pat[5] == '/' || pat[5] == '\0')))
		return addpat(head, Patprefix, pat, nil);
	/* Regex: compile and use */
	re = regcomp(pat);
	if(re == nil){
		lasterror = estrdup("invalid regex in query matches");
		return head;
	}
	head = addpat(head, Patregex, estrdup(pat), re);
	return head;
}

/*
 * Read one rule set: lines until blank line or # (blank terminates set).
 * Syntax (plumber-style):
 *   client is acme
 *   event is new
 *   query matches '*.go'
 *   rule set comfmt '// %s'
 *   rule set tabstop 8
 * Blank line terminates the rule set.
 */
static Rulerule*
readruleset(int *eof)
{
	char *line, *p;
	char *args[32];
	int n;
	Rulerule *r;
	Pathpat *pats;
	char *client, *event;
	char *dirbuf;
	int dirlen, ndir, have_line;

	pats = nil;
	client = nil;
	event = nil;
	dirbuf = nil;
	dirlen = 0;
	ndir = 0;
	have_line = 0;

	for(;;){
		line = getline();
		if(line == nil){
			*eof = 1;
			break;
		}
		input->lineno++;
		for(p = line; *p == ' ' || *p == '\t'; p++)
			;
		if(*p == '#' || *p == '\0'){
			/* blank or comment terminates rule set */
			if(have_line)
				break;
			continue;
		}
		if(include(p))
			continue;
		have_line = 1;
		/* rule set key value — parse from line before tokenize (value = rest of line) */
		if(strncmp(p, "rule", 4) == 0 && (p[4] == ' ' || p[4] == '\t')){
			char *rest, *keyend, *valstart, *valend;
			char *key, *val;
			int keylen, vlen;
			rest = p + 4;
			while(*rest == ' ' || *rest == '\t') rest++;
			if(strncmp(rest, "set", 3) == 0 && (rest[3] == ' ' || rest[3] == '\t')){
				rest += 3;
				while(*rest == ' ' || *rest == '\t') rest++;
				keyend = rest;
				while(*keyend != '\0' && *keyend != ' ' && *keyend != '\t' && *keyend != '\n' && *keyend != '\r')
					keyend++;
				keylen = keyend - rest;
				if(keylen > 0){
					valstart = keyend;
					while(*valstart == ' ' || *valstart == '\t') valstart++;
					valend = valstart;
					while(*valend != '\0' && *valend != '\n' && *valend != '\r') valend++;
					while(valend > valstart && (valend[-1] == ' ' || valend[-1] == '\t')) valend--;
					vlen = valend - valstart;
					key = emalloc(keylen + 1);
					memmove(key, rest, keylen);
					key[keylen] = '\0';
					if(vlen > 0){
						val = emalloc(vlen + 1);
						memmove(val, valstart, vlen);
						val[vlen] = '\0';
						if(vlen >= 2 && val[0] == '\'' && val[vlen-1] == '\''){
							val[vlen-1] = '\0';
							memmove(val, val + 1, vlen - 2);
							val[vlen-2] = '\0';
							vlen -= 2;
						}
					}else if(strcmp(key, "tabstop") == 0){
						val = estrdup("8");
					}else{
						free(key);
						key = nil;
						val = nil;
					}
					if(key != nil && val != nil){
						int need;
						need = (ndir > 0 ? 1 : 0) + keylen + 1 + strlen(val);
						dirbuf = dirbuf == nil ? emalloc(need + 1) : erealloc(dirbuf, dirlen + need + 1);
						if(ndir > 0){
							dirbuf[dirlen++] = ' ';
							dirbuf[dirlen] = '\0';
						}
						memmove(dirbuf + dirlen, key, keylen + 1);
						dirlen += keylen;
						dirbuf[dirlen++] = '=';
						dirbuf[dirlen] = '\0';
						memmove(dirbuf + dirlen, val, strlen(val) + 1);
						dirlen += strlen(val);
						free(key);
						free(val);
						ndir++;
					}
				}
				continue;
			}
		}
		n = tokenize(p, args, nelem(args));
		if(n == 0)
			continue;
		/* client is X */
		if(n >= 3 && strcmp(args[0], "client") == 0 && strcmp(args[1], "is") == 0){
			if(client != nil) free(client);
			client = estrdup(args[2]);
			continue;
		}
		/* event is X */
		if(n >= 3 && strcmp(args[0], "event") == 0 && strcmp(args[1], "is") == 0){
			if(event != nil) free(event);
			event = estrdup(args[2]);
			continue;
		}
		/* query matches 'pat' */
		if(n >= 3 && strcmp(args[0], "query") == 0 && strcmp(args[1], "matches") == 0){
			pats = parse_query_matches(args[2], pats);
			continue;
		}
		/* unknown line: skip (only blank/# terminates rule set) */
	}

	if(pats == nil){
		if(client) free(client);
		if(event) free(event);
		if(dirbuf) free(dirbuf);
		return nil;
	}
	r = emalloc(sizeof(Rulerule));
	r->pats = pats;
	r->client = client;
	r->event = event;
	r->directives = dirbuf != nil ? dirbuf : estrdup("");
	r->next = nil;
	return r;
}

Rulerule**
readrules(char *name, int fd)
{
	Rulerule *r, **out;
	int n, eof;

	parsing = 1;
	pushinput(name, fd, nil);
	out = emalloc(sizeof(Rulerule*));
	for(n = 0; ; n++){
		eof = 0;
		r = readruleset(&eof);
		if(r == nil && eof)
			break;
		if(r == nil)
			continue;
		out = erealloc(out, (n+2)*sizeof(Rulerule*));
		out[n] = r;
		out[n+1] = nil;
	}
	popinput();
	parsing = 0;
	return out;
}

Rulerule**
readrulesfromstring(char *s, int len)
{
	char *buf;
	Rulerule *r, **out;
	int n, eof;

	if(s == nil || len <= 0){
		out = emalloc(sizeof(Rulerule*));
		out[0] = nil;
		return out;
	}
	buf = emalloc(len+1);
	memmove(buf, s, len);
	buf[len] = '\0';

	parsing = 1;
	pushinput("<rules>", -1, (uchar*)buf);
	((Input*)input)->end = (uchar*)buf + len;
	out = emalloc(sizeof(Rulerule*));
	for(n = 0; ; n++){
		eof = 0;
		r = readruleset(&eof);
		if(r == nil && eof)
			break;
		if(r == nil)
			continue;
		out = erealloc(out, (n+2)*sizeof(Rulerule*));
		out[n] = r;
		out[n+1] = nil;
	}
	popinput();
	free(buf);
	parsing = 0;
	return out;
}

static char*
concat(char *s, char *t)
{
	if(t == nil)
		return s;
	if(s == nil)
		return estrdup(t);
	s = erealloc(s, strlen(s) + strlen(t) + 1);
	strcat(s, t);
	return s;
}

static char*
printrule(Rulerule *r)
{
	char *s, *key, *val;
	Pathpat *p;

	s = nil;
	if(r->client != nil){
		s = concat(s, "client is ");
		s = concat(s, r->client);
		s = concat(s, "\n");
	}
	if(r->event != nil){
		s = concat(s, "event is ");
		s = concat(s, r->event);
		s = concat(s, "\n");
	}
	for(p = r->pats; p != nil; p = p->next){
		s = concat(s, "query matches '");
		if(p->type == Patsuffix){
			s = concat(s, "*");
			s = concat(s, p->value);
		}else if(p->value != nil)
			s = concat(s, p->value);
		s = concat(s, "'\n");
	}
	/* directives: key=value -> rule set key value (value may contain spaces) */
	for(key = r->directives; key != nil && *key != '\0'; ){
		char kbuf[128];
		char vbuf[256];
		int klen, vlen;
		char *q, *r, *next_key;

		while(*key == ' ' || *key == '\t') key++;
		if(*key == '\0') break;
		val = strchr(key, '=');
		if(val == nil) break;
		klen = val - key;
		if(klen >= sizeof kbuf) klen = sizeof kbuf - 1;
		memmove(kbuf, key, klen);
		kbuf[klen] = '\0';
		val++;
		/* value runs until next " key=" (space then key then =) or end */
		next_key = nil;
		for(q = val; *q != '\0'; q++){
			if(*q != ' ' && *q != '\t') continue;
			r = q + 1;
			while(*r == ' ' || *r == '\t') r++;
			if(*r == '\0' || *r == '=') continue;
			while(*r != '\0' && *r != '=' && *r != ' ' && *r != '\t') r++;
			if(*r == '=')
				{ next_key = q; break; }
		}
		if(next_key != nil)
			vlen = next_key - val;
		else
			vlen = strlen(val);
		if(vlen >= sizeof vbuf) vlen = sizeof vbuf - 1;
		memmove(vbuf, val, vlen);
		vbuf[vlen] = '\0';
		s = concat(s, "rule set ");
		s = concat(s, kbuf);
		s = concat(s, " ");
		s = concat(s, vbuf);
		s = concat(s, "\n");
		key = next_key != nil ? next_key + 1 : val + vlen;
	}
	s = concat(s, "\n");
	return s;
}

char*
printrules(void)
{
	int i;
	char *s;

	s = nil;
	if(rules == nil)
		return estrdup("");
	for(i = 0; rules[i] != nil; i++)
		s = concat(s, printrule(rules[i]));
	if(s == nil)
		return estrdup("");
	return s;
}

char*
writerules(char *s, int n)
{
	static char *text;
	static long textlen;
	Rulerule **old;

	free(lasterror);
	lasterror = nil;
	if(s == nil || n == 0){
		/* end of write: parse accumulated content or clear */
		if(text != nil && textlen > 0){
			old = rules;
			rules = readrulesfromstring(text, textlen);
			if(old != nil)
				freerules(old);
		}else{
			old = rules;
			rules = emalloc(sizeof(Rulerule*));
			rules[0] = nil;
			if(old != nil)
				freerules(old);
		}
		free(text);
		text = nil;
		textlen = 0;
		return lasterror;
	}
	text = erealloc(text, textlen + n + 1);
	memmove(text + textlen, s, n);
	textlen += n;
	text[textlen] = '\0';
	return nil;	/* parse on close (n==0) */
}

void
parseerror(char *fmt, ...)
{
	char buf[512];
	va_list args;

	va_start(args, fmt);
	vseprint(buf, buf+sizeof buf, fmt, args);
	va_end(args);

	if(parsing){
		printinputstack();
		fprint(2, "%s\n", buf);
	}
	while(popinput())
		;
	lasterror = estrdup(buf);
}
