#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <signal.h>
#include "473_mm.h"

// initialization variables
void* VM; // ptr to the start of the v addr
int VM_SIZE; // size of vm
int N_FRAMES; // num of physical pages
int PAGE_SIZE; // size of both virtual and physical pages
int POLICY; // policy number

// linked list for fifo
typedef struct mm_fifo {
	int page_number;
	int place_in_pm;
	int flag;
	struct mm_fifo* next;
} fifo_page;

// circular linked list for tc replacement
typedef struct mm_tc
{
	int page_number;
	int place_in_pm;
	int flag;
	int r; // reference bit
	int m; // 'modified' bit
	struct mm_tc* next;
} tc_page;

// FIFO globals
fifo_page *fifo_head;
fifo_page *fifo_tail;
int pages_in_queue_fifo;

// TC globals
tc_page* tc_head; // ptr to the start of pm
tc_page* tc_tail; // ptr to the end of pm
tc_page* clock_head;
int pages_in_queue_tc;

// FIFO linked list headers
fifo_page* create_fifo_page(int page_number, int place_in_pm, int flag);
void new_fifo_page(int page_number, int place_in_pm, int flag);
void remove_oldest_fifo();

// TC linked list headers
tc_page* create_tc_page(int page_number, int place_in_pm, int flag, int r, int m);
void new_tc_page(int page_number, int place_in_pm, int flag, int r, int m);
void replace_tc_page(int evicted_virt_page, int virt_page);

// signal handler funcs
void fifo_handler (int sig, siginfo_t *sip, void *notused);
void tc_handler (int sig, siginfo_t *sip, void *notused);

void mm_init(void* vm, int vm_size, int n_frames, int page_size, int policy)
{
	// initialize the system
	VM = vm;
	VM_SIZE = vm_size;
	N_FRAMES = n_frames;
	PAGE_SIZE = page_size;
	POLICY = policy;

	// Protect VM pages to prevent unwanted modifications
	mprotect(VM, VM_SIZE, PROT_NONE); // the whole VM may not be accessed

	// sigaction initialization
	// ref: mst.edu, cmu.edu
	struct sigaction mm_handle;

	if (POLICY == 1) {
		mm_handle.sa_sigaction = fifo_handler; // fifo replacement policy
		pages_in_queue_fifo = 0;
		fifo_head = NULL;
		fifo_tail = NULL;
	}
	else if (POLICY == 2) {
		mm_handle.sa_sigaction = tc_handler; // tc: third chance
		tc_head = NULL;
		tc_tail = NULL;
		clock_head = NULL;
		pages_in_queue_tc = 0;
	}

	sigfillset(&mm_handle.sa_mask);
	mm_handle.sa_flags = SA_SIGINFO;

	// initiate sigaction
	sigaction(SIGSEGV, &mm_handle, NULL);
}


// FIFO
// Maintain a linked list of pages in sequence being brought to the PM
// Evict the one at the head in case of page fault
// Put the newly brought in page (from disk) at tail of the linked list

// if write, handle PROT_READ first then handle PROT_WRITE
void fifo_handler (int sig, siginfo_t *sip, void *notused){
	// mm_logger use only
	int virt_page = (sip->si_addr - VM) / PAGE_SIZE;
	int offset = (sip->si_addr - VM) % PAGE_SIZE;
	unsigned int phy_addr;
	
	// check the fifo queue to see if the page ref exists
	bool found = false;
	fifo_page *iter = fifo_head;
	
	while (iter) {
		if (iter->page_number == virt_page){
			found = true;
			// pm is trying to do a write
			// set (reset) page access permission to read/write
			mprotect(VM + PAGE_SIZE * virt_page, PAGE_SIZE, PROT_READ | PROT_WRITE);
			// store (change) the flag
			iter->flag = (PROT_READ | PROT_WRITE);

			// signal write fault
			phy_addr = iter->place_in_pm * PAGE_SIZE + offset;
			//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 1, virt_page, -1, 0, phy_addr);
			mm_logger(virt_page, 1, -1, 0, phy_addr); // cause: 1, evicted page: -1, write back: 0
			break;
		}
		iter = iter->next;
	}

	if (!found){ // page is not referenced in pm, signal page fault
		// mm_logger use only
		int evicted_virt_page = -1;
		int p_old_page;
		int write_back = 0;

		// case 1: no need for eviction
		if (pages_in_queue_fifo < N_FRAMES){
			//printf("case1\n");
			pages_in_queue_fifo++;
		}
		else { // evict the oldest page
			//printf("case2\n");
			mprotect(VM + PAGE_SIZE * fifo_head->page_number, PAGE_SIZE, PROT_NONE); // reset the VM area to non accessible
				
			// oldest page has been written before, hence needs to write back to disk
			if (fifo_head->flag == (PROT_READ | PROT_WRITE)) write_back = 1;

			// remove the first page
			evicted_virt_page = fifo_head->page_number;
			p_old_page = fifo_head->place_in_pm;
			remove_oldest_fifo();
		}
		// bring in page into pm
		if (fifo_head == NULL){
			fifo_head = create_fifo_page(virt_page, 0, PROT_READ); // read only
			fifo_tail = fifo_head;
		}
		else{
			if (evicted_virt_page == -1){
				new_fifo_page(virt_page, pages_in_queue_fifo - 1, PROT_READ);
			}
			else {
				new_fifo_page(virt_page, p_old_page, PROT_READ); // new page actually sits in the same place as the evicted page
			}
		}

		// protect the newly referenced page addr
		mprotect(VM + PAGE_SIZE * virt_page, PAGE_SIZE, PROT_READ);

		// call mm_logger to log the page fault
		if (evicted_virt_page == -1){ // if no page is evicted
			phy_addr = (pages_in_queue_fifo - 1) * PAGE_SIZE + offset; 

			//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 0, virt_page, evicted_virt_page, 0, phy_addr);
			mm_logger(virt_page, 0, -1, 0, phy_addr); // cause: 0, evicted page: -1, write back: 0
		}
		else {
			phy_addr = p_old_page * PAGE_SIZE + offset;

			//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 0, virt_page, evicted_virt_page, 1, phy_addr);
			mm_logger(virt_page, 0, evicted_virt_page, write_back, phy_addr); // cause:0, evicted page: depends, write back: depends
		}
	}
}

// TC
// if r = 0, m = 0: evict this page
// if r = 1, m = 0: set r to 0
// if r = 1, m = 1: first pass: r = 0, m = 1; second pass: skip; 
// 					third pass: evict and write back (r = 0, m = 0)
void tc_handler (int sig, siginfo_t *sip, void *notused){
	// mm_logger use only
	int virt_page = (sip->si_addr - VM) / PAGE_SIZE;
	int offset = (sip->si_addr - VM) % PAGE_SIZE;
	unsigned int phy_addr;

	bool found = false;
	tc_page *iter = tc_head;

	//printf("virt page: %d\n", virt_page);

	while (iter) {
		if (iter->page_number == virt_page){
			//printf("yes\n");
			found = true;
			bool signal = false;
			int cause = 1; // default as write fault, can change to 2

			// when the page has been modified and has passed 2nd chance, reset r bit
			// no permission changes
			if (iter->flag == PROT_NONE) { // r has been reset
				iter->r = 1; // change r bit back to 1
				cause = 2; // change cause to 2

				// set (reset) page access permission to read only (to catch subsequent write)
				mprotect(VM + PAGE_SIZE * virt_page, PAGE_SIZE, PROT_READ);
				// store (change) the flag
				iter->flag = PROT_READ;

				signal = true;
			}
			else {
				// pm is trying to do a write
				// set the page's r and m bit to 1
				if (iter->m == 0) signal = true;

				iter->r = 1;
				iter->m = 1; // key step: set the modified bit

				// set (reset) page access permission to read/write
				mprotect(VM + PAGE_SIZE * virt_page, PAGE_SIZE, PROT_READ | PROT_WRITE);
				// store (change) the flag
				iter->flag = (PROT_READ | PROT_WRITE);
			}

			// signal write fault
			if (signal) {
				phy_addr = iter->place_in_pm * PAGE_SIZE + offset;
				//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 1, virt_page, -1, 0, phy_addr);
				mm_logger(virt_page, cause, -1, 0, phy_addr); // cause: depends, evicted page: -1, write back: 0
			}

			break;
		}

		if (iter == tc_tail) break; // only scan through the mem once
		iter = iter->next;
	}

	if (!found) {
		mprotect(VM + PAGE_SIZE * virt_page, PAGE_SIZE, PROT_READ); // grant read to the new page for the below two cases

		if (pages_in_queue_tc < N_FRAMES){ // case one, no eviction needed
			if (tc_head == NULL){
				tc_head = create_tc_page(virt_page, 0, PROT_READ, 1, 0); // page, place, prot, r, m
				tc_tail = tc_head;
				clock_head = tc_head; // align the clock head to the head of the PM
			}
			else{
				new_tc_page(virt_page, pages_in_queue_tc, PROT_READ, 1, 0); // npage, placement, prot, r, m
			}

			// log it
			phy_addr = pages_in_queue_tc * PAGE_SIZE + offset; 
			//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 0, virt_page, -1, 0, phy_addr);
			mm_logger(virt_page, 0, -1, 0, phy_addr); // cause: 0, evicted page: -1, write back: 0

			pages_in_queue_tc++;
		}
		else { // need to evict a page using tc replacement
			int write_back = 0; // set default write back val to 0

			bool page_found = false;
			while (!page_found) {
				int r = clock_head->r;
				int m = clock_head->m;

				if (r == 0 && m == 0) { // case 1
					page_found = true;
					break;
				}
				else if (r == -1 && m == 1) { // page is modified, but already had its second chance
					page_found = true;
					write_back = 1; // since page has been modified, write back
					break;
				}
				else if (r == 1 && m == 0) { // case 2
					clock_head->r = 0;
					// disable read/write just to catch future change to this page
					clock_head->flag = PROT_NONE;
					mprotect(VM + PAGE_SIZE * clock_head->page_number, PAGE_SIZE, PROT_NONE);
					
				}
				else if (r == 1 && m == 1) { // case 3
					clock_head->r = 0; // give first chance
					// disable read/write just to catch future change to this page
					clock_head->flag = PROT_NONE;
					mprotect(VM + PAGE_SIZE * clock_head->page_number, PAGE_SIZE, PROT_NONE);
				}
				else if (r == 0 && m == 1) { // self-defined case
					clock_head->r = -1; // give second chance, set r to -1
				}

				// keep going or back to head until we can evict a page
				clock_head = clock_head->next;
			}

			tc_page* evict = clock_head;
			int evicted_virt_page = evict->page_number;
			int place = evict->place_in_pm;

			// disable permissions to the old page
			mprotect(VM + PAGE_SIZE * evicted_virt_page, PAGE_SIZE, PROT_NONE); // disable read/write for the old mem addr
			clock_head = clock_head->next; // move the clock head to the right before proceding
			replace_tc_page(evicted_virt_page, virt_page); // replace the evicted page with the new page at its location

			// log the read fault
			phy_addr = place * PAGE_SIZE + offset;
			//printf("cause: %d, vp: %d, evp: %d, wb: %d, off: %d\n", 0, virt_page, evicted_virt_page, 1, phy_addr);
			mm_logger(virt_page, 0, evicted_virt_page, write_back, phy_addr); // cause:0, evicted page: depends, write back: depends
		}
	}

	// debug use only: print pages in PM
	/*
	tc_page *print = tc_head;
	while(print){
		printf("tc: vp: %d, place: %d, r: %d, m: %d\n", print->page_number, print->place_in_pm, print->r, print->m);
		if (print == tc_tail) break;
		print = print->next;
	}
	printf("\n");
	*/
}


////////////////////////////////////////////////////////////////////////////
// helpers for maintaining fifo linked list
fifo_page* create_fifo_page(int page_number, int place_in_pm, int flag){
	fifo_page* tmp = (fifo_page*)malloc(sizeof(fifo_page));
	tmp->page_number = page_number;
	tmp->place_in_pm = place_in_pm;
	tmp->flag = flag;
	tmp->next = NULL;

	return tmp;
}

void new_fifo_page(int page_number, int place_in_pm, int flag){
	fifo_page *new_page = create_fifo_page(page_number, place_in_pm, flag);
	fifo_tail->next = new_page;
	fifo_tail = new_page; // tail now points to the new page
}

void remove_oldest_fifo(){
	fifo_page* tmp = fifo_head;
	fifo_head = fifo_head->next;
	free(tmp);
}

////////////////////////////////////////////////////////////////////////////
// helpers for maintaining tc linked list (circular)
tc_page* create_tc_page(int page_number, int place_in_pm, int flag, int r, int m){
	tc_page* tmp = (tc_page*)malloc(sizeof(tc_page));
	tmp->page_number = page_number;
	tmp->place_in_pm = place_in_pm;
	tmp->flag = flag;
	tmp->r = r;
	tmp->m = m;
	tmp->next = NULL;

	return tmp;
}

void new_tc_page(int page_number, int place_in_pm, int flag, int r, int m){
	tc_page *new_page = create_tc_page(page_number, place_in_pm, flag, r, m);

	tc_tail->next = new_page;
	new_page->next = tc_head;

	tc_tail = new_page; // tail now points to the new page
}

void replace_tc_page(int evicted_virt_page, int virt_page){ 
	// works the same way as putting the new page to the end since the clock head is moved one page to the right
	tc_page* tmp = tc_head;

	while (tmp->page_number != evicted_virt_page) tmp = tmp->next; 

	tmp->page_number = virt_page;
	tmp->flag = PROT_READ;
	tmp->r = 1;
	tmp->m = 0;
}
