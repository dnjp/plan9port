#include "common.h"

char *_MAILROOT =	"#9/mail";
char *_UPASLOG =		"#9/sys/log";
char *_UPASLIB = 	"#9/mail/lib";
char *_UPASBIN=		"#9/bin/upas";
char *_UPASTMP = 	"/var/tmp";
char *_SHELL = 		"#9/bin/rc";
char *_POST =		"#9/sys/lib/post/dispatch";

int MBOXMODE = 0662;

void
upasconfig(void)
{
	static int did;
	char *e;

	if(did)
		return;
	did = 1;
	_MAILROOT = unsharp(_MAILROOT);
	_UPASLOG = unsharp(_UPASLOG);
	_UPASLIB = unsharp(_UPASLIB);
	_UPASBIN = unsharp(_UPASBIN);
	_SHELL = unsharp(_SHELL);
	_POST = unsharp(_POST);

	/*
	 * Personal mail layout (no symlinks in $PLAN9/mail/lib): set before running
	 * upas/send, upas/marshal, upas/aliasmail, etc.
	 *   export UPASLIB=$HOME/mail/lib
	 *   export MAILROOT=$HOME/mail
	 */
	e = getenv("UPASLIB");
	if(e != nil && *e != '\0')
		_UPASLIB = strdup(e);
	e = getenv("MAILROOT");
	if(e != nil && *e != '\0')
		_MAILROOT = strdup(e);
}
