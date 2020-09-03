#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <thread.h>
#include <curthread.h>
#include <uio.h>
#include <clock.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <vnode.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */


struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	/*
	 * Initialize as needed.
	 */	

	as->head = NULL; // page table empty initially
	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	// base of the stack . Stack goes from as_stackvbase -> stacktop
	as->as_stackvbase = USERSTACK - 32 * PAGE_SIZE;	
	as->as_stacknPages = 32;	// number of pages held by the stack  
	as->as_heapStart = 0;	// start point of the heap
	as->as_heapEnd = 0;	// end point of the heap
	as->permissionv1 = 0;
	as->permissionv2 = 0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	assert(old != NULL);	
	struct addrspace *newas;
	
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	newas->as_vbase1 = old->as_vbase1;
	newas->as_npages1 = old->as_npages1;
	newas->as_npages2 = old->as_npages2;
	newas->as_vbase2 = old->as_vbase2;
	newas->as_stackvbase = old->as_stackvbase;
	newas->as_stacknPages = old->as_stacknPages;
	newas->as_heapStart = old->as_heapStart;
	newas->as_heapEnd = old->as_heapEnd;
	newas->permissionv1 = old->permissionv1;
	newas->permissionv2 = old->permissionv2;
	newas->head = copypagetable(old, newas);

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	assert(as != NULL);	
	deletepagetable(as);
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	int i, spl;
	(void)as;
	spl = splhigh();
	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	assert(as != NULL);	

	size_t numpages; 
	int spl = splhigh();
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	
	sz = (sz + PAGE_SIZE -1) & PAGE_FRAME;

	numpages = sz/PAGE_SIZE;

	if (as->as_vbase1 == 0){
		as->as_vbase1 = vaddr;
		as->permissionv1 = (readable|writeable|executable);
		as->as_npages1 = numpages;
		as->as_heapStart = (as->as_vbase1 & PAGE_FRAME) + ((as->as_npages1 + 1) * PAGE_SIZE);
		as->as_heapEnd = as->as_heapStart;
		splx(spl);
		return 0;
	}
	
	if(as->as_vbase2 == 0){
		as->as_vbase2 = vaddr;
		as->permissionv2 = (readable|writeable|executable); 
		as->as_npages2 = numpages;
		as->as_heapStart = (as->as_vbase2 & PAGE_FRAME) + ((as->as_npages2 + 1) * PAGE_SIZE);
		as->as_heapEnd = as->as_heapStart;
		splx(spl);
		return 0;
	}
	
	kprintf("\nNot enough space buddy :)\n");

	splx(spl);
	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	assert(as != NULL);	
	as->permissionv1 = 6;	//read and write
	as->permissionv2 = 6;	//read and write
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	assert(as != NULL);	
	as->permissionv1 = 5;	//read and executable
	as->permissionv2 = 6;	//read and write
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Initial user-level stack pointer */
	assert(as != NULL);
	*stackptr = USERSTACK;
	return 0;
}

//find it page table entry already exists in page table
struct ptentry * 
findentry(struct addrspace * as, vaddr_t vaddress){	
	assert(as != NULL);	
	struct ptlist * templist = as->head;
	assert((vaddress & PAGE_FRAME) == vaddress);	

	//no entries in the page table, return null	
	if(templist == NULL){
		return NULL;
	}

	while(templist != NULL){
		assert(templist->entry->vaddress < 0x80000000);		
		if(templist->entry->vaddress == vaddress){
			return templist->entry;
		}
		templist = templist->next; 		
	}

	return NULL;
}

struct ptentry * 
addentry(struct addrspace * as, vaddr_t vaddress, paddr_t paddress){
	struct ptentry * tempentry = (struct ptentry *)kmalloc(sizeof(struct ptentry));
	struct ptlist * templist = (struct ptlist *)kmalloc(sizeof(struct ptlist));

	// lock_acquire(page_lock);
	int spl = splhigh();

	//kprintf("Adding entry for PID: %d at %x\n", curthread->pid, vaddress);
	tempentry->vaddress = vaddress;
	tempentry->paddress = paddress; 
	tempentry->location = 0;
	tempentry->ondisk = 0;
	tempentry->count = 1;
	// tempentry->next = NULL;

	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop, heapstart, heapend; 

	vbase1 = (as->as_vbase1 & PAGE_FRAME);
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = (as->as_vbase2 & PAGE_FRAME);
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = as->as_stackvbase;
	stacktop = USERSTACK;
	heapstart = as->as_heapStart;
	heapend = as->as_heapEnd;
	
	if(vaddress >= vbase1 && vaddress < vtop1){
		tempentry->permission = as->permissionv1;
	}else if(vaddress >= vbase2 && vaddress < vtop2){
		tempentry->permission = as->permissionv2;
	}else if(vaddress >= stackbase && vaddress < stacktop){
		tempentry->permission = 7;
	}else if(vaddress >= heapstart && vaddress < heapend){
		tempentry->permission = 7;
	}
	//trying with curthread address space instead of given address space even though they should be the same
	templist->entry = tempentry;
	templist->next = NULL;

	templist->next = curthread->t_vmspace->head;
	curthread->t_vmspace->head = templist; 
	splx(spl);
	return tempentry; 
}

struct ptentry * 
swapentry(struct addrspace * as, struct ptentry * oldentry, paddr_t paddress){
	struct ptentry * tempentry = (struct ptentry *)kmalloc(sizeof(struct ptentry));
	struct ptlist * templist = as->head;
	//should probably check to see if kmalloc returns null or if vaddress is valid


	int spl = splhigh();

	tempentry->vaddress = oldentry->vaddress;
	tempentry->paddress = paddress; 
	tempentry->location = 0;
	tempentry->ondisk = 0;
	tempentry->count = 1;
	tempentry->permission = oldentry->permission;

	oldentry->count --;
	assert(oldentry->count != 0);

	while(templist != NULL){
		if(templist->entry->vaddress == oldentry->vaddress){
			templist->entry = tempentry;
			break;
		}
		templist = templist->next;
	}

	splx(spl);

	//trying with curthread address space instead of given address space even though they should be the same
	as_activate(NULL);
	return tempentry; 
}

struct ptentry * 
copyentry(struct addrspace * as, struct ptentry * oldentry, paddr_t paddress){
	struct ptentry * tempentry = (struct ptentry *)kmalloc(sizeof(struct ptentry));
	struct ptlist * templist = as->head;
	//should probably check to see if kmalloc returns null or if vaddress is valid


	int spl = splhigh();

	tempentry->vaddress = oldentry->vaddress;
	tempentry->paddress = paddress; 
	tempentry->location = 0;
	tempentry->ondisk = 0;
	tempentry->count = 1;
	tempentry->permission = oldentry->permission;

	oldentry->count --;
	assert(oldentry->count != 0);

	while(templist != NULL){
		if(templist->entry->vaddress == oldentry->vaddress){
			templist->entry = tempentry;
			break;
		}
		templist = templist->next;
	}

	memcpy((void *)PADDR_TO_KVADDR(paddress), (const void *)PADDR_TO_KVADDR(oldentry->paddress), PAGE_SIZE);

	splx(spl);

	//trying with curthread address space instead of given address space even though they should be the same
	as_activate(NULL);
	return tempentry; 
}

struct ptlist *
copypagetable(struct addrspace * old, struct addrspace * newas){
	assert(old != NULL && newas != NULL);
	if(old->head == NULL){
		return NULL;
	}

	struct ptlist * templist = old->head;
	struct ptlist * newtable = NULL;

	lock_acquire(page_lock); 
	int spl = splhigh();	
	while(templist != NULL){
		struct ptlist * newlist = (struct ptlist *)kmalloc(sizeof(struct ptlist));

		newlist->entry = templist->entry;
		newlist->next = NULL;
		templist->entry->count ++;
		
		newlist->next = newtable;
		newtable = newlist;
		templist = templist->next;
	}

	as_activate(NULL);
	splx(spl);
	lock_release(page_lock);
	return newtable;
}

void 
deletepagetable(struct addrspace * as){
	assert(as != NULL);	
	struct ptlist * listdelete;
	struct ptlist * templist;
	struct ptentry * todelete;

	as_activate(NULL);
	lock_acquire(page_lock);
	int spl = splhigh();
	templist = as->head;
	while(templist != NULL){
		listdelete = templist; 
		if(listdelete->entry->ondisk == 0){

			if(listdelete->entry->location != 0 && listdelete->entry->count <=1){
				offsetavailable[listdelete->entry->location] = 0;
			}

			if(listdelete->entry->count <= 1){
				int index = listdelete->entry->paddress/PAGE_SIZE;
				todelete = listdelete->entry;

				coremap[index]->as = NULL;
				coremap[index]->vaddress = 0;
				coremap[index]->status = FREE;
				
				kfree(todelete);
			}else{
				listdelete->entry->count --;
			}

			listdelete->entry = NULL;

		}else if(listdelete->entry->ondisk == 1){
			assert(listdelete->entry->location > 0);
			assert(offsetavailable[listdelete->entry->location] == 1);
			if(listdelete->entry->count <=1 ){
				offsetavailable[listdelete->entry->location] = 0;
				todelete = listdelete->entry;
				kfree(todelete);
			}
			else{
				listdelete->entry->count --;
			}
		}
		
		templist = templist->next;
		kfree(listdelete);		
	}
	as_activate(NULL);
	splx(spl);
	lock_release(page_lock);
	as->head = NULL;
}


void 
swapin(struct ptentry * tempentry, int swapindex){
	lock_acquire(core_lock);	
	int offset = tempentry->location; 
	assert(offset > 0);

	struct uio tempuio; 
	mk_kuio(&tempuio, (void *)PADDR_TO_KVADDR(swapindex*PAGE_SIZE), PAGE_SIZE, offset*PAGE_SIZE, UIO_READ);

	VOP_READ(bigswap, &tempuio);
	
	tempentry->paddress = swapindex * PAGE_SIZE;
	tempentry->ondisk = 0;

	coremap[swapindex]->as = curthread->t_vmspace;
	coremap[swapindex]->vaddress = tempentry->vaddress;
	coremap[swapindex]->status = DIRTY;
	
	time_t secs;
	u_int32_t nsecs;
	gettime(&secs, &nsecs);
	coremap[swapindex]->secs = secs;
	coremap[swapindex]->nsecs = nsecs;
	lock_release(core_lock);
}

void swapout(int index){	
	lock_acquire(core_lock);
	if(coremap[index]->status == FREE){
		lock_release(core_lock);
		return;	
	}

	int offset = 0;	

	struct ptlist * templist = coremap[index]->as->head;

	while(templist != NULL){
		if(templist->entry->vaddress == coremap[index]->vaddress){
			offset = templist->entry->location;
			break;
		}
		templist = templist->next;
	}

	struct ptentry * tempentry = templist->entry;

	if(offset == 0){
		int i;
		for(i = 1; i < 1280; i++){
			if(offsetavailable[i] == 0){
				offset = i;				
				offsetavailable[i] = 1;
				break;
			}
		}
	}

	struct uio tempuio; 
	mk_kuio(&tempuio, (void *)PADDR_TO_KVADDR(index*PAGE_SIZE), PAGE_SIZE, offset*PAGE_SIZE, UIO_WRITE);

	int result = VOP_WRITE(bigswap, &tempuio);
	if(result){
		kprintf("VOP WRITE FAILED Error: %d\n", result);
	}

	tempentry = findentry(coremap[index]->as, coremap[index]->vaddress);

	tempentry->paddress = 0;
	tempentry->location = offset; 
	tempentry->ondisk = 1;
	
	coremap[index]->as = NULL;
	coremap[index]->vaddress = 0;
	coremap[index]->status = FREE;

	time_t secs;	
	u_int32_t nsecs;
	gettime(&secs, &nsecs);
	coremap[index]->secs = secs;
	coremap[index]->nsecs = nsecs;

	//Flush TLB
	as_activate(NULL);
	lock_release(core_lock);
}

void printtableandcore(struct addrspace * as, int table, int core){
	int spl = splhigh();
	struct ptlist * tempcheck = as->head;
	int index;

	if(table == 1){
		kprintf("\n\nTable for PID: %d\n\n", curthread->pid);
		while(tempcheck != NULL){
			kprintf("index: %d, paddr: %x vaddr: %x, count: %d \n", tempcheck->entry->paddress/PAGE_SIZE, tempcheck->entry->paddress, tempcheck->entry->vaddress, tempcheck->entry->count);
			tempcheck = tempcheck->next;
		}
	}
	if(core == 1){
		kprintf("\n\nCoremap PID: %d\n\n", curthread->pid);
		for(index = num_trash + 1; index < totalnumpages; index++){
			if(coremap[index]->status == DIRTY){
				kprintf("index: %d status: %d vaddr: %x\n", index, coremap[index]->status, coremap[index]->vaddress);
			}
		}
	}
	splx(spl);
}



