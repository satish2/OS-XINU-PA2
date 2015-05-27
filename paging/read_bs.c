#include <conf.h>
#include <kernel.h>
#include <mark.h>
#include <bufpool.h>
#include <proc.h>
#include <paging.h>

SYSCALL read_bs(char *dst, bsd_t store, int page) {

	if (check_BS_ID(store) || check_PG_ID(page)) {
		return SYSERR;
	}

	void * phy_addr = BACKING_STORE_BASE + (store << 19) + (page * NBPG);
	bcopy(phy_addr, (void*) dst, NBPG);
	return OK;
}

