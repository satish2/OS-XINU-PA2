/* vgetmem.c - vgetmem */

#include <conf.h>
#include <kernel.h>
#include <mem.h>
#include <proc.h>
#include <paging.h>

extern struct pentry proctab[];
/*------------------------------------------------------------------------
 * vgetmem  --  allocate virtual heap storage, returning lowest WORD address
 *------------------------------------------------------------------------
 */
WORD *vgetmem(unsigned int nbytes) {

	/*
	 * referred to getmem.c
	 */
	STATWORD ps;
	struct mblock *remain;

	disable(ps);
	nbytes = (unsigned int) roundmb(nbytes);

	struct pentry *pptr = &proctab[currpid];
	struct mblock *vmlist = &pptr->vmemlist;

	if (vmlist->mnext == NULL || nbytes == 0) {
		restore(ps);
		return (WORD*) ( SYSERR);

	}

	struct mblock *prev = vmlist;
	struct mblock *next = vmlist->mnext;

	while (next != NULL) {
		if (next->mlen > nbytes) {
			remain = next + nbytes;
			remain->mnext = next->mnext;
			remain->mlen = next->mlen - nbytes;
			prev->mnext = remain;
			restore(ps);
			return next;
		} else if (next->mlen == nbytes) {
			prev->mnext = next->mnext;
			restore(ps);
			return next;
		}
		prev = next;
		next = next->mnext;
	}

	restore(ps);
	return (WORD*) ( SYSERR);

}
