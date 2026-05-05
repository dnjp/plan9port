#include "common.h"

char *MAILROOT = "#9/mail";
char *SPOOL	= "#9/mail";
char *UPASLOG	= "#9/sys/log";
char *UPASLIB	= "#9/mail/lib";
char *UPASBIN	= "#9/bin/upas";
char *UPASTMP	= "/var/tmp";
char *SHELL	= "#9/bin/rc";

void
upasconfig(void)
{
	static int did;

	if(did)
		return;
	did = 1;
	MAILROOT = unsharp(MAILROOT);
	SPOOL = unsharp(SPOOL);
	UPASLOG = unsharp(UPASLOG);
	UPASLIB = unsharp(UPASLIB);
	UPASBIN = unsharp(UPASBIN);
	SHELL = unsharp(SHELL);
}
