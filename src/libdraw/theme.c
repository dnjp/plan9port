#include <u.h>
#include <libc.h>
#include <draw.h>
#include <fcntl.h>
#include <thread.h>

Colors lightcolors = {
	/* tag (blue) */
	.tagback       = 0xEAFFFEFF,   /* allocimagemix(DPalebluegreen, DWhite) */
	.taghi         = 0x9EEEEEFF,   /* DPalegreygreen */
	.tagbord       = 0x8888CCFF,   /* DPurpleblue */
	.tagtext       = 0x000000FF,

	/* text (yellow) */
	.textback      = 0xFFFFEAFF,   /* allocimagemix(DPaleyellow, DWhite) */
	.texthi        = 0xEEEE9EFF,   /* DDarkyellow */
	.textbord      = 0x99994CFF,   /* DYellowgreen */
	.texttext      = 0x000000FF,

	/* chrome */
	.background    = 0x777777FF,
	.winback       = 0xFFFFFFFF,
	.winhi         = 0xCCCCCCFF,
	.winbord       = 0x999999FF,
	.wintext       = 0x000000FF,
	.titlecol      = 0x55AAAAFF,   /* DGreygreen */
	.lighttitlecol = 0x9EEEEEFF,   /* DPalegreygreen */
	.holdcol       = 0x000099FF,   /* DMedblue */
	.lightholdcol  = 0x005DBBFF,   /* DGreyblue */
	.paleholdcol   = 0x4993DDFF,   /* DPalegreyblue */
	.paletextcol   = 0x666666FF,
	.sizecol       = 0xFF0000FF,   /* DRed */

	/* acme buttons */
	.modbutton     = 0x000099FF,   /* DMedblue */
	.but2col       = 0xAA0000FF,
	.but3col       = 0x006600FF,

	/* sam */
	.darkgrey      = 0x444444FF,

	/* 9term */
	.termred       = 0xDD0000FF,
	.termgrey      = 0xEEEEEEFF,
	.termdarkgrey  = 0x666666FF,
};

Colors darkcolors = {
	/* tag (candlelit blue-green) */
	.tagback       = 0x999C8CFF,
	.taghi         = 0x6F9383FF,
	.tagbord       = 0x61597AFF,
	.tagtext       = 0x000000FF,

	/* text (candlelit parchment) */
	.textback      = 0xA49E83FF,
	.texthi        = 0x9B955DFF,
	.textbord      = 0x6C6735FF,
	.texttext      = 0x000000FF,

	/* chrome */
	.background    = 0x555048FF,
	.winback       = 0xA49E8DFF,
	.winhi         = 0x887E70FF,
	.winbord       = 0x6C6357FF,
	.wintext       = 0x000000FF,
	.titlecol      = 0x456E63FF,
	.lighttitlecol = 0x6F9383FF,
	.holdcol       = 0x000064FF,
	.lightholdcol  = 0x004377FF,
	.paleholdcol   = 0x38618AFF,
	.paletextcol   = 0x4C4B40FF,
	.sizecol       = 0xCC1818FF,

	/* acme buttons */
	.modbutton     = 0x000064FF,
	.but2col       = 0x7A1212FF,
	.but3col       = 0x1A4A1AFF,

	/* sam */
	.darkgrey      = 0x39382EFF,

	/* 9term */
	.termred       = 0xAA1818FF,
	.termgrey      = 0x9B918BFF,
	.termdarkgrey  = 0x4C4B40FF,
};



#ifdef __APPLE__
#include <sys/event.h>
#include <fcntl.h>
#include <stdio.h>

int
themeisdark(void)
{
	/*
	 * popen defaults(1) for a guaranteed fresh read.
	 * CFPreferencesCopyAppValue caches per-process and won't
	 * reflect changes made by cfprefsd in another process.
	 */
	FILE *f = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
	if(f == nil)
		return 0;
	char buf[32] = {0};
	fgets(buf, sizeof buf, f);
	pclose(f);
	return strstr(buf, "Dark") != nil;
}

static int wpipe[2] = {-1, -1};

static void
watchproc(void *v)
{
	USED(v);
	char *home;
	char path[512];
	int kq, fd;
	struct kevent ev, out;

	home = getenv("HOME");
	if(home == nil)
		return;
	snprint(path, sizeof path,
		"%s/Library/Preferences/.GlobalPreferences.plist", home);

	kq = kqueue();
	if(kq < 0){
		fprint(2, "theme: kqueue: %r\n");
		return;
	}

	fd = open(path, O_RDONLY);
	if(fd < 0){
		fprint(2, "theme: open %s: %r\n", path);
		return;
	}

	EV_SET(&ev, fd, EVFILT_VNODE,
		EV_ADD|EV_ENABLE|EV_CLEAR,
		NOTE_WRITE|NOTE_DELETE|NOTE_RENAME, 0, nil);
	kevent(kq, &ev, 1, nil, 0, nil);

	for(;;){
		if(kevent(kq, nil, 0, &out, 1, nil) <= 0)
			continue;

		/* macOS writes prefs atomically via tmp+rename.
		 * NOTE_RENAME fires on the old fd; reopen to re-arm. */
		if(out.fflags & (NOTE_DELETE|NOTE_RENAME)){
			close(fd);
			fd = open(path, O_RDONLY);
			if(fd < 0)
				return;
			EV_SET(&ev, fd, EVFILT_VNODE,
				EV_ADD|EV_ENABLE|EV_CLEAR,
				NOTE_WRITE|NOTE_DELETE|NOTE_RENAME, 0, nil);
			kevent(kq, &ev, 1, nil, 0, nil);
		}

		/* notify acme's relay */
		char b = 1;
		write(wpipe[1], &b, 1);
	}
}

int
themewatchfd(void)
{
	if(wpipe[0] >= 0)
		return wpipe[0];
	if(pipe(wpipe) < 0)
		return -1;
	fcntl(wpipe[1], F_SETFL, O_NONBLOCK);
	proccreate(watchproc, nil, 32*1024);
	return wpipe[0];
}

#else
int  themeisdark(void) { return 0; }
int  themewatchfd(void){ return -1; }
#endif