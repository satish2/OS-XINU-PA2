/* pfint.c - pfint */

#include <conf.h>
#include <kernel.h>
#include <paging.h>
#include <proc.h>

/*-------------------------------------------------------------------------
 * pfint - paging fault ISR
 *-------------------------------------------------------------------------
 */
SYSCALL pfint() {

	STATWORD ps;
	disable(ps);

	int store, pageth;
	//Step 1: Get the faulted address 'a'
	unsigned long a = read_cr2();

	//Step 3: Let pd point to the current page directory.
	pd_t *pd = proctab[currpid].pdbr + sizeof(pd_t) * (PD_OFFSET(a)); //current page directory

	//Step 4: Check if 'a' is a legal address i.e., it has been mapped or not. If not, print error message and kill process.
	if ((bsm_lookup(currpid, a, &store, &pageth) == SYSERR) || (store == -1) || (pageth == -1)) {
//		kprintf("%\nlu : Not Legal Address. This is not mapped\n", a);
		kill(currpid);
		restore(ps);
		return (SYSERR);
	}

	//Step 7 & 8: If no page table entry exists, obtain a frame for it and initialize it.
	//Paging in the faulted page

	if (pd->pd_pres == 0) {
		int new_frame = create_PageTable(currpid);
		if (new_frame == -1) {
//			kprintf("pfint: Cannot create page table\n");
			return SYSERR;
		}
		pd->pd_pres = 1;
		pd->pd_write = 1;
		pd->pd_base = new_frame + FRAME0;
		/*
		 * In the inverted page table increment the reference count of the frame which holds pt.
		 * This indicates that one more of pt's entries is marked "present."
		 */
		frm_tab[(unsigned int) pd / NBPG - FRAME0].fr_refcnt++;
	}
	int frame;
	frame = get_frm();
	if (frame == -1) {
		kprintf("pfint: failed to get new frame .\n");
		kill(currpid);
		restore(ps);
		return SYSERR;
	}
	//frame = Obtain a free frame
	frm_tab[frame].fr_dirty = CLEAN;
	frm_tab[frame].fr_loadtime = -1;
	frm_tab[frame].fr_pid = currpid;
	frm_tab[frame].fr_refcnt = 1;
	frm_tab[frame].fr_status = MAPPED;
	frm_tab[frame].fr_type = FR_PAGE;
	frm_tab[frame].fr_vpno = (a / NBPG) & 0x000fffff;

	//Copying page 'pageth' of store 'store' to frame.
	read_bs((frame + FRAME0) * NBPG, store, pageth);

	if (page_replace_policy == FIFO)
		insert_Frame_FIFO(frame);
	else if (page_replace_policy == LRU) {
		LRU_updateTimeCount();
	}

	pt_t *pageTableEntry = (pd->pd_base) * NBPG + sizeof(pt_t) * (PT_OFFSET(a));
	pageTableEntry->pt_pres = 1;
	pageTableEntry->pt_write = 1;
	pageTableEntry->pt_base = frame + FRAME0;
	frm_tab[(unsigned int) pageTableEntry / NBPG - FRAME0].fr_refcnt++;

	write2CR3(currpid);
	restore(ps);
	return OK;
}

