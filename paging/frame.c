/* frame.c - manage physical frames */
#include <conf.h>
#include <kernel.h>
#include <proc.h>
#include <paging.h>
fr_map_t frm_tab[NFRAMES];
void clearPageTableEntry(pt_t *pt_entry);
void clearFrameTableEntry(pd_t *pd_entry, int i, int isFR_PAGE);
void frm_invalidate_TLB(int frm_num);
int write2CR3(int pid);
int globalPageTable[4];
/*-------------------------------------------------------------------------
 * init_frm - initialize frm_tab
 *-------------------------------------------------------------------------
 */
SYSCALL init_frm() {
	int i;
	for (i = 0; i < NFRAMES; i++) {
		frm_tab[i].fr_dirty = 0;
		frm_tab[i].fr_loadtime = -1;
		frm_tab[i].fr_pid = -1;
		frm_tab[i].fr_status = UNMAPPED;
		frm_tab[i].fr_refcnt = 0;
		frm_tab[i].fr_type = -1;
		frm_tab[i].fr_vpno = -1;
	}
	return OK;
}

/*-------------------------------------------------------------------------
 * get_frm - get a free frame according page replacement policy
 *-------------------------------------------------------------------------
 */
SYSCALL get_frm() {
	int i;
	for (i = 0; i < NFRAMES; i++) {
		if (frm_tab[i].fr_status == UNMAPPED) {
			return i;
		}
	}
	kprintf("Page Replacement Policy %d\n",grpolicy());
	if (grpolicy() == FIFO) {
		return getFrame_FIFO();
	}
	if (grpolicy() == LRU) {
		int j = getFrame_LRU();
		if (j == SYSERR)
			return SYSERR;

		free_frm(j + FRAME0);
		return j;
	}
	return -1;
}

int getFrame_FIFO() {

	int j;
	FifoQueue *FIFO_pick = &head_FIFO;
	for (j = 0; j < n_FIFOPages; j++) {
		FIFO_pick = FIFO_pick->nextFrame;
	}
	freemem(FIFO_pick, sizeof(FifoQueue));
	if (n_FIFOPages > 0)
		n_FIFOPages--;
	free_frm(FIFO_pick->frameID + FRAME0);
	kprintf("Frame %d is replaced.\n", FIFO_pick->frameID);
	freemem(FIFO_pick, sizeof(FifoQueue));
	return FIFO_pick->frameID;
}

/*-------------------------------------------------------------------------
 * free_frm - free a frame
 *-------------------------------------------------------------------------
 */
SYSCALL free_frm(int i) {

	if (check_FR_ID(i)) {
		return SYSERR;
	}

	switch (frm_tab[i].fr_type) {
	case FR_PAGE: {
		int store, pageth;
		if ((bsm_lookup(frm_tab[i].fr_pid, frm_tab[i].fr_vpno * NBPG, &store, &pageth) == SYSERR) || (store == -1) || (pageth == -1)) {
			return SYSERR;
		}
		write_bs((i + FRAME0) * NBPG, store, pageth);
		pd_t *pd_entry = proctab[frm_tab[i].fr_pid].pdbr + (frm_tab[i].fr_vpno / 1024) * sizeof(pd_t);
		pt_t *pt_entry = (pd_entry->pd_base) * NBPG + (frm_tab[i].fr_vpno % 1024) * sizeof(pt_t);
		if (pt_entry->pt_pres == 0) {
			return SYSERR;
		}
		clearPageTableEntry(pt_entry);
		clearFrameTableEntry(pd_entry, i, 1);

		return OK;
	}
	case FR_TBL: {
		int j;
		for (j = 0; j < 1024; j++) {
			pt_t *pt_entry = (i + FRAME0) * NBPG + j * sizeof(pt_t);
			if (pt_entry->pt_pres == 1) {
				free_frm(pt_entry->pt_base - FRAME0);
			}
		}
		for (j = 0; j < 1024; j++) {
			pd_t *pd_entry = proctab[frm_tab[i].fr_pid].pdbr + j * sizeof(pd_t);
			if (pd_entry->pd_base - FRAME0 == i) {
				pd_entry->pd_pres = 0;
				pd_entry->pd_write = 1;
				pd_entry->pd_user = 0;
				pd_entry->pd_pwt = 0;
				pd_entry->pd_pcd = 0;
				pd_entry->pd_acc = 0;
				pd_entry->pd_mbz = 0;
				pd_entry->pd_fmb = 0;
				pd_entry->pd_global = 0;
				pd_entry->pd_avail = 0;
				pd_entry->pd_base = 0;
			}
		}

		clearFrameTableEntry(NULL, i, 0);
		return OK;
	}
	case FR_DIR: {
		int j;
		for (j = 4; j < 1024; j++) {
			pd_t *pd_entry = proctab[frm_tab[i].fr_pid].pdbr + j * sizeof(pd_t);
			if (pd_entry->pd_pres == 1) {
				free_frm(pd_entry->pd_base - FRAME0);
			}
		}
		clearFrameTableEntry(NULL, i, 0);
		return OK;
	}
	}
	return SYSERR;
}

void clearPageTableEntry(pt_t *pt_entry) {
	pt_entry->pt_acc = 0;
	pt_entry->pt_avail = 0;
	pt_entry->pt_base = 0;
	pt_entry->pt_dirty = 0;
	pt_entry->pt_global = 0;
	pt_entry->pt_mbz = 0;
	pt_entry->pt_pcd = 0;
	pt_entry->pt_pres = 0;
	pt_entry->pt_pwt = 0;
	pt_entry->pt_write = 1;
	pt_entry->pt_user = 0;
}

void clearFrameTableEntry(pd_t *pd_entry, int i, int isFR_PAGE) {

	frm_tab[i].fr_dirty = CLEAN;
	frm_tab[i].fr_loadtime = -1;
	frm_tab[i].fr_pid = -1;
	frm_tab[i].fr_refcnt = 0;
	frm_tab[i].fr_status = UNMAPPED;
	frm_tab[i].fr_type = -1;
	frm_tab[i].fr_vpno = -1;

	if (isFR_PAGE) {
		frm_tab[pd_entry->pd_base - FRAME0].fr_refcnt--;
		if (frm_tab[pd_entry->pd_base - FRAME0].fr_refcnt <= 0) {
			free_frm(pd_entry->pd_base - FRAME0);
		}
	}

}

int getFrame_LRU() {
	STATWORD ps;
	unsigned long int min = 429496729;
	int i;
	int retFrame = 0;
	fr_map_t *tmp_frame;

	for (i = 0; i < NFRAMES; i++) {
		tmp_frame = &frm_tab[i];
		if (tmp_frame->fr_type == FR_PAGE) {
			if (tmp_frame->fr_loadtime < min) {
				min = tmp_frame->fr_loadtime;
				retFrame = i;
			}

			else if (tmp_frame->fr_loadtime == min) {
				if (frm_tab[retFrame].fr_vpno < tmp_frame->fr_vpno)
					retFrame = i;
			}
		}
	}
	restore(ps);
	kprintf("%d frame is replaced\n",retFrame);
	return retFrame;
}

int LRU_updateTimeCount() {
	STATWORD ps;
	int i, j, pid;
	struct pentry *pptr;
	unsigned long pdbr;
	pd_t *pageDir_entry;
	pt_t *pageTable_entry;
	int frame;

	disable(ps);
	LRU_Count++;

	for (pid = 1; pid < NPROC; pid++) {
		pptr = &proctab[pid];
		if (pptr->pstate != PRFREE) {
			pdbr = pptr->pdbr;
			pageDir_entry = pdbr;

			for (i = 0; i < 1024; i++) {
				if (pageDir_entry->pd_pres == 1) {
					pageTable_entry = (pageDir_entry->pd_base) * NBPG;
					for (j = 0; j < 1024; j++) {
						if (pageTable_entry->pt_pres == 1 && pageTable_entry->pt_acc == 1) {
							frame = pageTable_entry->pt_base - FRAME0;
							pageTable_entry->pt_acc = 0;
							frm_tab[frame].fr_loadtime = LRU_Count;
						}
						pageTable_entry++;
					}
				}
				pageDir_entry++;
			}
		}
	}

	restore(ps);
	return OK;
}

void insert_Frame_FIFO(int i) {

//	kprintf("Inserted page in FIFO Queue as FIFO is page replacement policy %d\n", i);
	FifoQueue *FIFO_tmp = (FifoQueue *) getmem(sizeof(FifoQueue));
	FIFO_tmp->frameID = i;
	FIFO_tmp->nextFrame = head_FIFO.nextFrame;
	head_FIFO.nextFrame = FIFO_tmp;
	n_FIFOPages++;
}

int create_PageTable(int pid) {

	int i;
	int frame = get_frm();
	if (frame == -1) {
		return SYSERR;
	}

	frm_tab[frame].fr_refcnt = 0;
	frm_tab[frame].fr_type = FR_TBL;
	frm_tab[frame].fr_dirty = CLEAN;
	frm_tab[frame].fr_loadtime = -1;
	frm_tab[frame].fr_status = MAPPED;
	frm_tab[frame].fr_pid = pid;
	frm_tab[frame].fr_vpno = -1;


	for (i = 0; i < 1024; i++) {
		pt_t *pt = (FRAME0 + frame) * NBPG + i * sizeof(pt_t);
		pt->pt_dirty = CLEAN;
		pt->pt_mbz = 0;
		pt->pt_global = 0;
		pt->pt_avail = 0;
		pt->pt_base = 0;
		pt->pt_pres = 0;
		pt->pt_write = 1;
		pt->pt_user = 0;
		pt->pt_pwt = 0;
		pt->pt_pcd = 0;
		pt->pt_acc = 0;
	}
	return frame;
}

int create_PageDirectory(int pid) {
	int i;
	int frame = get_frm();

	if (frame == -1) {
		return -1;
	}
	frm_tab[frame].fr_dirty = CLEAN;
	frm_tab[frame].fr_loadtime = -1;
	frm_tab[frame].fr_pid = pid;
	frm_tab[frame].fr_refcnt = 4;
	frm_tab[frame].fr_status = MAPPED;
	frm_tab[frame].fr_type = FR_DIR;
	frm_tab[frame].fr_vpno = -1;

	proctab[pid].pdbr = (FRAME0 + frame) * NBPG;
	for (i = 0; i < 1024; i++) {
		pd_t *pd_entry = proctab[pid].pdbr + (i * sizeof(pd_t));
		pd_entry->pd_pcd = 0;
		pd_entry->pd_acc = 0;
		pd_entry->pd_mbz = 0;
		pd_entry->pd_fmb = 0;
		pd_entry->pd_global = 0;
		pd_entry->pd_avail = 0;
		pd_entry->pd_base = 0;
		pd_entry->pd_pres = 0;
		pd_entry->pd_write = 1;
		pd_entry->pd_user = 0;
		pd_entry->pd_pwt = 0;

		if (i < 4) {
			pd_entry->pd_pres = 1;
			pd_entry->pd_write = 1;
			pd_entry->pd_base = globalPageTable[i];
		}
	}
	return frame;
}

int initializeGlobalPageTable() {
	int i, j, k;
	for (i = 0; i < 4; i++) {
		k = create_PageTable(NULLPROC);
		if (k == -1) {
			return SYSERR;
		}
		globalPageTable[i] = FRAME0 + k;

		for (j = 0; j < 1024; j++) {
			pt_t *pt = globalPageTable[i] * NBPG + j * sizeof(pt_t);

			pt->pt_pres = 1;
			pt->pt_write = 1;
			pt->pt_base = j + i * 1024;

			frm_tab[k].fr_refcnt++;
		}
	}
	return OK;
}

int write2CR3(int pid) {
	unsigned int pdbr = (proctab[pid].pdbr) / NBPG;

	if ((frm_tab[pdbr - FRAME0].fr_status != MAPPED) || (frm_tab[pdbr - FRAME0].fr_type != FR_DIR) || (frm_tab[pdbr - FRAME0].fr_pid != pid)) {
		return SYSERR;
	}
	write_cr3(proctab[pid].pdbr);
	return OK;
}
