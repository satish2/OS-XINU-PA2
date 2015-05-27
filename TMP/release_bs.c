#include <conf.h>
#include <kernel.h>
#include <proc.h>
#include <paging.h>

SYSCALL release_bs(bsd_t bs_id) {

	if (check_BS_ID(bs_id)) {
		return SYSERR;
	}
	free_bsm(bs_id);
	write2CR3(currpid);
	return OK;
}

