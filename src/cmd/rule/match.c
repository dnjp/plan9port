#include <u.h>
#include <libc.h>
#include <regexp.h>
#include <ruler.h>
#include "ruler.h"

/*
 * Expand $home in prefix pattern; return static buffer or allocated.
 */
static char*
expandprefix(char *val)
{
	char *p;
	static char buf[1024];

	if(val == nil)
		return nil;
	if(strncmp(val, "$home", 5) != 0)
		return val;
	if(home == nil)
		return val;
	p = val + 5;
	if(*p == '/' || *p == '\0'){
		snprint(buf, sizeof buf, "%s%s", home, p);
		return buf;
	}
	return val;
}

static int
patmatch(Pathpat *p, char *query)
{
	char *val;
	int nq, nv;
	Resub sub[1];

	if(query == nil)
		return 0;
	switch(p->type){
	case Patsuffix:
		nq = strlen(query);
		nv = strlen(p->value);
		return nv <= nq && strcmp(query + nq - nv, p->value) == 0;
	case Patprefix:
		val = expandprefix(p->value);
		return strncmp(query, val, strlen(val)) == 0;
	case Patexact:
		return strcmp(query, p->value) == 0;
	case Patregex:
		if(p->regex == nil)
			return 0;
		return regexec((Reprog*)p->regex, query, sub, 1);
	default:
		return 0;
	}
}

static int
rulepathmatch(Rulerule *r, char *query)
{
	Pathpat *p;

	/* no path patterns means match any path (used for type-only rules like "type is win") */
	if(r->pats == nil)
		return 1;
	for(p = r->pats; p != nil; p = p->next)
		if(patmatch(p, query))
			return 1;
	return 0;
}

/*
 * Find end of value: value runs until next " key=" (space then key=)
 * or next key= (no space: digit/letter then '=' starts next pair).
 * Returns pointer past the last byte of value (start of next pair or end).
 */
static char*
value_end(char *v)
{
	char *s, *r, *eq;

	/* Try " key=" (space then key=) first */
	for(s = v; *s != '\0'; s++){
		if(*s != ' ' && *s != '\t')
			continue;
		r = s + 1;
		while(*r == ' ' || *r == '\t')
			r++;
		if(*r == '\0' || *r == '=')
			continue;
		while(*r != '\0' && *r != '=' && *r != ' ' && *r != '\t')
			r++;
		if(*r == '=')
			return s;
	}
	/* No " key=" found: look for next '=' preceded by key (non-space) */
	for(eq = v; (eq = strchr(eq, '=')) != nil; eq++){
		if(eq <= v)
			continue;
		if(eq[-1] == ' ' || eq[-1] == '\t')
			continue;
		/* key runs backward from eq-1; value ends at key start */
		r = eq;
		while(r > v && r[-1] != ' ' && r[-1] != '\t')
			r--;
		return r;
	}
	return s;
}

/*
 * Trim value [vstart,vend): set *lo to first non-space, *hi to last non-space+1.
 */
static void
trim_value(char *vstart, char *vend, char **lo, char **hi)
{
	*lo = vstart;
	*hi = vend;
	while(*lo < *hi && (**lo == ' ' || **lo == '\t'))
		(*lo)++;
	while(*hi > *lo && ((*hi)[-1] == ' ' || (*hi)[-1] == '\t'))
		(*hi)--;
}

/*
 * True if value [vlo,vhi) contains space, tab, or single quote (needs quoting).
 */
static int
value_needs_quotes(char *vlo, char *vhi)
{
	char *s;
	for(s = vlo; s < vhi; s++)
		if(*s == ' ' || *s == '\t' || *s == '\'')
			return 1;
	return 0;
}

/*
 * Convert space/tab-separated key=value directives to newline-separated lines.
 * Values that contain space, tab, or ' are emitted as key='value' (with ''
 * for '); simple values (e.g. numbers, on/off) are emitted unquoted so
 * clients can preserve type (e.g. tabstop=8). Caller frees result.
 */
static char*
directives_to_lines(char *directives)
{
	char *out, *p, *eq, *vstart, *vend, *vlo, *vhi;
	int n, cap, need, vlen, quoted;
	int i;

	if(directives == nil || *directives == '\0')
		return estrdup("");
	cap = 256;
	out = emalloc(cap);
	n = 0;
	for(p = directives; *p != '\0'; ){
		while(*p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;
		eq = strchr(p, '=');
		if(eq == nil || eq == p)
			break;
		vstart = eq + 1;
		vend = value_end(vstart);
		trim_value(vstart, vend, &vlo, &vhi);
		if(vhi <= vlo)
			vhi = vlo;
		vlen = vhi - vlo;
		quoted = value_needs_quotes(vlo, vhi);
		if(quoted)
			need = (eq - p) + 2 + vlen*2 + 2 + 1;  /* key= + ' + value ('' per ') + ' + \n */
		else
			need = (eq - p) + 1 + vlen + 1 + 1;    /* key= + value + \n */
		if(n + need > cap){
			while(n + need > cap)
				cap *= 2;
			out = erealloc(out, cap);
		}
		memmove(out + n, p, eq - p);
		n += eq - p;
		out[n++] = '=';
		if(quoted){
			out[n++] = '\'';
			for(i = 0; i < vlen; i++){
				if(vlo[i] == '\'')
					out[n++] = '\'';
				out[n++] = vlo[i];
			}
			out[n++] = '\'';
		}else
			for(i = 0; i < vlen; i++)
				out[n++] = vlo[i];
		out[n++] = '\n';
		out[n] = '\0';
		p = vend;
		if(*p == ' ' || *p == '\t')
			p++;
	}
	return out;
}

char*
matchquery(char *query, char *client, char *event, char *type)
{
	int i, nrules;
	Rulerule *r;

	if(rules == nil){
		dlog("matchquery: no rules loaded");
		return nil;
	}
	if(query == nil)
		return nil;

	for(nrules = 0; rules[nrules] != nil; nrules++)
		;
	dlog("matchquery: query=%s client=%s event=%s type=%s nrules=%d",
		query, client?client:"(nil)", event?event:"(nil)", type?type:"(nil)", nrules);

	for(i = 0; rules[i] != nil; i++){
		r = rules[i];
		if(!rulepathmatch(r, query)){
			dlog("  rule[%d]: no path match (client=%s)", i, r->client?r->client:"*");
			continue;
		}
		if(r->client != nil && (client == nil || strcmp(client, r->client) != 0)){
			dlog("  rule[%d]: path matched but client mismatch (want %s got %s)", i, r->client, client?client:"(nil)");
			continue;
		}
		if(r->event != nil && (event == nil || strcmp(event, r->event) != 0)){
			dlog("  rule[%d]: path+client matched but event mismatch (want %s got %s)", i, r->event, event?event:"(nil)");
			continue;
		}
		if(r->type != nil && (type == nil || strcmp(type, r->type) != 0)){
			dlog("  rule[%d]: path+client+event matched but type mismatch (want %s got %s)", i, r->type, type?type:"(nil)");
			continue;
		}
		dlog("  rule[%d]: MATCH -> %s", i, r->directives);
		return directives_to_lines(r->directives);
	}
	dlog("matchquery: no rule matched");
	return nil;
}
