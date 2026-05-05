#include "common.h"
#include <libsec.h>
#include "dat.h"

int
mtreecmp(Avl *va, Avl *vb)
{
	Mtree *a, *b;

	a = (Mtree*)va;
	b = (Mtree*)vb;
	return memcmp(a->m->x.digest, b->m->x.digest, SHA1dlen);
}

int
mtreeisdup(Mailbox *mb, Message *m)
{
	Mtree t;

	assert(Topmsg(mb, m) && m->x.digest);
	if(!m->x.digest)
		return 0;
	memset(&t, 0, sizeof t);
	t.m = m;
	if(lookupavl(mb->mtree, &t.avl))
		return 1;
	return 0;
}

Message*
mtreefind(Mailbox *mb, uchar *digest)
{
	Message m0;
	Mtree t, *p;

	memset(&m0, 0, sizeof m0);
	m0.x.digest = digest;
	memset(&t, 0, sizeof t);
	t.m = &m0;
	if(p = (Mtree*)lookupavl(mb->mtree, &t.avl))
		return p->m;
	return nil;
}

void
mtreeadd(Mailbox *mb, Message *m)
{
	Avl *old;
	Mtree *p;

	assert(Topmsg(mb, m) && m->x.digest);
	p = emalloc(sizeof *p);
	p->m = m;
	old = nil;
	insertavl(mb->mtree, &p->avl, &old);
	assert(old == nil);
}

void
mtreedelete(Mailbox *mb, Message *m)
{
	Avl *old;
	Mtree t, *p;

	assert(Topmsg(mb, m));
	memset(&t, 0, sizeof t);
	t.m = m;
	if(m->deleted & ~Deleted){
		if(m->x.digest == nil)
			return;
		p = (Mtree*)lookupavl(mb->mtree, &t.avl);
		if(p == nil || p->m != m)
			return;
		old = nil;
		deleteavl(mb->mtree, &t.avl, &old);
		free((Mtree*)old);
		return;
	}
	assert(m->x.digest);
	old = nil;
	deleteavl(mb->mtree, &t.avl, &old);
	if(old == nil)
		assert("mtree delete fails");
	free((Mtree*)old);
}
