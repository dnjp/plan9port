/*
 * Internal header for ruler daemon (rule structs, pattern types).
 * Public protocol is in include/ruler.h.
 */
typedef struct Pathpat Pathpat;
typedef struct Rulerule Rulerule;

enum
{
	Patsuffix,
	Patprefix,
	Patexact,
	Patregex,
};

struct Pathpat
{
	int	type;	/* Patsuffix, Patprefix, Patexact, Patregex */
	char	*value;	/* pattern string; for Patregex, compiled form in regex */
	void	*regex;	/* when type==Patregex: compiled Reprog* for regexec */
	Pathpat	*next;
};

struct Rulerule
{
	Pathpat		*pats;
	char		*client;	/* nil = match any */
	char		*event;		/* nil = match any */
	char		*directives;	/* space-separated key=value; returned as newline-separated */
	Rulerule	*next;
};

/* rules.c */
void		parseerror(char*, ...);
void		error(char*, ...);
void*		emalloc(ulong);
void*		erealloc(void*, ulong);
char*		estrdup(char*);
Rulerule**	readrules(char*, int);
Rulerule**	readrulesfromstring(char*, int);
void		freerules(Rulerule**);
char*		printrules(void);
char*		writerules(char*, int);
void		printinputstack(void);
int		popinput(void);

/* match.c */
char*		matchquery(char *query, char *client, char *event);

/* 9p.c */
void		startfsys(int);
void		reloadrulesfromfile(void);

extern Rulerule	**rules;
extern char	*rulerfile;
extern char	*user;
extern char	*home;
extern char	*lasterror;
extern int	verbose;

/* verbose logging: when verbose is set, prints to stderr with "ruler: " prefix */
void		dlog(char*, ...);
