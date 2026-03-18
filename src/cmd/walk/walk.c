/*
 *  Walk a directory tree, in the style of du(1),
 *  but with some additional flourishes.
 *
 *  Dan Cross <cross@math.psu.edu>
 */

#include <u.h>
#include <libc.h>

static int	mkdepth(int);
static char	*mkname(char *, int *, char *, char *);
static void	walk(char *, int, int);
static void	walkname(char *, int, int);

char	*fmt;

void
main(int argc, char *argv[])
{
	char	*dir;
	int	depth;
	Dir	*d;

	dir = ".";
	fmt = "%s\n";
	depth = -1;
	ARGBEGIN {
	case 'd':
		depth = atoi(ARGF());
		break;
	case 'q':
		quotefmtinstall();
		doquote = needsrcquote;
		fmt = "%q\n";
		break;
	}ARGEND
	if (argc == 0)
		walkname(".", depth, 1);
	else {
		for (dir = *argv; dir; dir = *++argv) {
			if ((d = dirstat(dir)) == nil) {
				fprint(2, "dirstat %s: %r\n", dir);
				continue;
			}
			walkname(dir, depth, d->mode & DMDIR);
			free(d);
		}
	}
		
	exits(0);
}

static void
walkname(char *dirname, int depth, int isdir)
{
	int	fd;

	if (strcmp(dirname, ".") != 0 && strcmp(dirname, "..") != 0)
		print(fmt, dirname);
	if (isdir) {
		fd = open(dirname, OREAD);
		if (fd < 0) {
			fprint(2, "open %s: %r\n", dirname);
			return;
		}
		walk(dirname, fd, depth);
		close(fd);
	}
}

static char *
mkname(char *name, int *l, char *basename, char *filename)
{
	char	*nname;
	int	t;

	t = strlen(basename) + 1 + strlen(filename) + 1;
	if (*l == 0 || name == nil) {
		*l = t;
		name = malloc(t);
		if (name == nil)
			sysfatal("malloc %d: %r\n", l);
	} else if (*l < t) {
		nname = realloc(name, t);
		if (nname == nil) {
			free(name);
			sysfatal("malloc %d: %r\n", l);
		}
		*l = t;
		name = nname;
	}
	snprint(name, t, "%s/%s", basename, filename);
	cleanname(name);

	return(name);
}

static int
mkdepth(int depth)
{

	return((depth == -1) ? depth : depth - 1);
}

static void
walk(char *dirname, int fd, int depth)
{
	Dir	*dir, *dp;
	char	*name;
	int	i, l, n;

	if (depth == 0)
		return;
	l = 0;
	name = nil;
	n = dirreadall(fd, &dir);
	for (dp = dir, i = 0; i < n; dp++, i++) {
		if (strcmp(dp->name, ".") == 0 || strcmp(dp->name, "..") == 0)
			continue;
		name = mkname(name, &l, dirname, dp->name);
		walkname(name, mkdepth(depth), dp->mode & DMDIR);
	}
	free(dir);
	if (name != nil)
		free(name);
}
