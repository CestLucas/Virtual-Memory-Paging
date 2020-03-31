Virtual Memory -- Paging
Requirements
You are required to implement the below interface function in 473_mm.c (defined in 473_mm.h):
void mm_init(void* vm, int vm_size , int n_frames, int page_size, int policy);

This function initializes your memory management system. You can use this call to initialize the data structures and perform other set up operations (signal handlers etc). The following are passed as input to this function:-

vm: denotes the pointer to the start of the virtual address space that needs to be managed.
vm_size: denotes the size of the virtual address space in bytes (starting from *vm).
n_frames: denotes the number of physical pages available in your system.
page_size: denotes the size of both virtual and physical pages.
policy: can take values 1 or 2 -- 1 indicates FIFO page replacement policy and 2 indicates clock replacement policy.
Note: The virtual memory (vm*) is already allocated and page aligned when this call is made. Though 'page fault' and 'write-back' in a normal system will involve disk read/write respectively, you DO NOT NEED TO ACTUALLY MAINTAIN SWAP SPACE OR PHYSICAL MEMORY SPACE in your implementation. You only need to keep appropriate data structures to track the required statistics and report them through the mm_logger() function below.
After mm_init(), there will be no specific call in our test code to explicitly state that a page is being read/written (load/store), i.e. there are NO function calls for each access to a page. This should be automatically inferred/captured by your code by implementing access protection on the virtual address space (pointed to by vm*) through (mprotect()) system calls, and the accesses (reads and writes) made by the user program will automatically get trapped and the signal handler that you will have to write to catch the SIGSEGV signals will have to emulate what happens in the paging system.  A useful pointer to how mprotect() operates can be found  here . The 'mprotect()' function specifies the desired protection-level or access permission for virtual memory pages. A program violating the specified access permission will receive a 'SIGSEGV' signal.

You will need to write a signal handler to catch this 'SIGSEGV' signal and perform appropriate actions required by the specific replacement policy. A useful pointer on how to set signal handlers using 'sigaction()' function can be found  here . Your SIGSEGV signal handler should emulate  the Virtual Memory system. It should emulate the behavior of a page fault handler. It should also call the following function (which is provided in project3.c) with the appropriate parameters to log the progress of the execution:

void mm_logger(int cause, int virt_page, int evicted_virt_page, int write_back, int phy_addr);

cause: Indicates what caused the SIGSEGV. 0 is for a Read access to a non-present page, 1 is for a Write access to a Read-Only page, and 2 indicates neither Read or Write fault but was needed to tracking references - only when the page is present and would not incur a fault in the actual hardware, but is needed in your emulation to set the Reference bit in case it was reset (note that if the Reference bit was already one, a signal should NOT have been raised!).

virt_page: This is the page number of the virtual page being referenced.

evicted_virt_page: If this fault evicts a page from physical memory, this represents the virtual page number that is evicted. In case of no page is evicted, you should pass -1.

write_back: This indicates whether the evicted page needs to be written back to disk. If there is write back it should be 1, otherwise it should be 0.

phy_addr: This represents the physical address (frame_number concatenated with the offset).

Page Replacement Algorithms
Below is a brief description of the two page replacement algorithm that you will have to implement in your signal handler, 

(i) First-In-First-Out Replacement:

As the name suggests, this page replacement policy evicts the oldest virtual page (among the currently resident pages in physical memory) brought in to the physical memory. You will have to protect the pages that are NOT in physical memory to catch accesses to these page and record page faults. Initially, all virtual pages will have read/write permissions turned off, so that you catch any access to any of those pages. Once you decide to bring in a page, you will mprotect() it, in Read-Only mode, and it is possible you get a fault to the same page again which will indicate that the program is trying to do a store (and consequently you have to change it to Read-Write mode) - this is one way to figure out whether the access is a load or a store. When you evict a virtual page, you will mprotect() it, so that any future access to it will raise a SIGSEGV, and you will record it as a page fault.

However, you should minimize the number of SIGSEGVs, i.e. if there is no page fault and/or actions to be done by the handler, you should NOT raise a signal

(ii) Third chance Replacement:

In this algorithm, you will maintain a circular linked list, with the head pointer pointing to the next potential candidate page for eviction as discussed in class. You will be maintaining "Reference bit" and "Modified bit" for each page in physical memory. When looking for the next candidate page for eviction, if by any chance, the page pointed by the head pointer has its Reference bit on (set as one), the head pointer resets this bit (sets it to zero), and moves to the next element in the circular list and retries. When the head finds a candidate with 'Reference Bit' set as zero, it becomes a candidate for replacement as per the second chance page replacement algorithm. However, in our "third chance algorithm", you will need to give such a page a third chance if it has been modified (modified bit is set to one) since pages requiring write-back will incur higher overheads for replacement.

A physical page under the clock head could be in one of the following 3 states: (a) R=0, M=0,; (b) R=1, M=0 or (c) R=1, M=1;  In case (a), the page can be immediately evicted. In case (b), you should give it a second chance, i.e. reset the R bit, and if the next time the clock head comes to that page its bits indicate state (a), then replace it. In case (c), you should give it a third chance, i.e. reset the R bit in the 1st pass, and even in the 2nd pass that the head comes to that page, you should skip it - be careful, if you reset the M bit, you will not know whether this page needs to be written back to disk later on at the time of replacement. Only the third time, should it be replaced (and written back). However,note it is possible that between the 2nd pass and the 3rd pass, the R bit could again change to 1 in which case it will again get skipped in the 3rd and 4th pass and would get evicted only in the 5th pass (as long as there is no further reference before then).

NOTE  that the reference bit is not really accessible to you as a user program, and the only way for you to emulate this is by taking SIGSEGV signals appropriately. So, for a page in physical memory for which the reference bit is off (zero), you may still require to mprotect() to take a fault on either a load or store to that page. In fact, you may need to take 2 additional signals for that page - the first (read/write) to turn on reference bit, and the second for a store (to turn on the modified bit). For a page in physical memory for which the reference bit is on, you would need to mprotect it in Read-Only mode until the first write to it (to update the modified bit). Turning off reference bits would implying doing appropriate mprotect()s. As in FIFO, please note that you should try to minimize the number of SIGSEGVs raised/handled for emulation of these actions.
