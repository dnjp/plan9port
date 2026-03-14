/*
 * Multi-entry subfont cache for acme, replacing the single-entry version in
 * libdraw. With two fonts in use (var body + fix body), the 1-entry cache
 * thrashes constantly. NSUBFCACHE entries covers all subfonts needed for
 * typical ASCII source files across both fonts.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>

#define NSUBFCACHE 64

typedef struct Subfontentry Subfontentry;
struct Subfontentry {
	char	*name;
	Subfont	*sf;
};

static Subfontentry cache[NSUBFCACHE];
static int ncache;

Subfont*
lookupsubfont(Display *d, char *name)
{
	int i;

	for(i = 0; i < ncache; i++){
		if(cache[i].name != nil && strcmp(name, cache[i].name) == 0){
			if(cache[i].sf->bits->display == d){
				cache[i].sf->ref++;
				/* move to front for LRU */
				if(i > 0){
					Subfontentry e = cache[i];
					memmove(cache+1, cache, i*sizeof(Subfontentry));
					cache[0] = e;
				}
				return cache[0].sf;
			}
		}
	}
	return nil;
}

void
installsubfont(char *name, Subfont *subfont)
{
	int i;

	/* if already present, update in place and move to front */
	for(i = 0; i < ncache; i++){
		if(cache[i].name != nil && strcmp(name, cache[i].name) == 0){
			cache[i].sf = subfont;
			if(i > 0){
				Subfontentry e = cache[i];
				memmove(cache+1, cache, i*sizeof(Subfontentry));
				cache[0] = e;
			}
			return;
		}
	}

	/* evict LRU (last entry) if full */
	if(ncache == NSUBFCACHE){
		free(cache[NSUBFCACHE-1].name);
		ncache--;
	}

	/* insert at front */
	memmove(cache+1, cache, ncache*sizeof(Subfontentry));
	cache[0].name = strdup(name);
	cache[0].sf = subfont;
	ncache++;
}

void
uninstallsubfont(Subfont *subfont)
{
	int i;

	for(i = 0; i < ncache; i++){
		if(cache[i].sf == subfont){
			free(cache[i].name);
			memmove(cache+i, cache+i+1, (ncache-i-1)*sizeof(Subfontentry));
			ncache--;
			return;
		}
	}
}
