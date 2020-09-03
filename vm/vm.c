#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <machine/vm.h>
#include <clock.h>
#include <uio.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <vnode.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
 * alloc_kpages() and free_kpages() are called by kmalloc() and thus the whole
 * kernel will not boot if these 2 functions are not completed.
 */

int isBooted = 0; 

int
load_segment(struct vnode *v, off_t offset, vaddr_t vaddr, 
    size_t memsize, size_t filesize,
    int is_executable)
{
    struct uio u;
    int result;
    size_t fillamt;

    if (filesize > memsize) {
        kprintf("ELF: warning: segment filesize > segment memsize\n");
        filesize = memsize;
    }

    DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx from Thread: %d\n", 
        (unsigned long) filesize, (unsigned long) vaddr, curthread->pid);

    u.uio_iovec.iov_ubase = (userptr_t)vaddr;
    u.uio_iovec.iov_len = memsize;   // length of the memory space
    u.uio_resid = filesize;          // amount to actually read
    u.uio_offset = offset;
    u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = curthread->t_vmspace;

    result = VOP_READ(v, &u);
    if (result) {
        return result;
    }

    if (u.uio_resid != 0) {
        /* short read; problem with executable? */
        kprintf("ELF: short read on segment - file truncated?\n");
        return ENOEXEC;
    }

    /* Fill the rest of the memory space (if any) with zeros */
    fillamt = memsize - filesize;
    if (fillamt > 0) {
        DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n", 
              (unsigned long) fillamt);
        u.uio_resid += fillamt;
        result = uiomovezeros(fillamt, &u);
    }

    return result;
 }

paddr_t
getppages(unsigned long numpages){

	int spl = splhigh();	
	paddr_t paddress;

	paddress = ram_stealmem(numpages);
	splx(spl);

	return paddress;
}


void
vm_bootstrap(void)
{
	paddr_t first_paddr, last_paddr, freeaddr;
	
	core_sem = sem_create("coremap sem", 1);
	core_lock = lock_create("corelock"); 
	page_lock = lock_create("pagelock");
	
	ram_getsize(&first_paddr, &last_paddr);
	totalnumpages = (int)(last_paddr)/PAGE_SIZE;

	coremap = (struct coremapblock **)PADDR_TO_KVADDR(first_paddr);
	freeaddr = first_paddr + (totalnumpages * sizeof(struct coremapblock));
	num_trash = freeaddr/PAGE_SIZE + 1;	

	int i = 0;
	for(i = 0; i < totalnumpages; i++){
		coremap[i] = (struct coremapblock *)PADDR_TO_KVADDR(first_paddr + sizeof(coremap) + i * sizeof(struct coremapblock));
		coremap[i]->as = NULL;
		coremap[i]->vaddress = 0;
		
		if(i < num_trash){
			coremap[i]->status = TRASH;
		}else{
			coremap[i]->status = FREE;
		}
	}

	isBooted = 1;
	firstbigswap = 0;
	for(i = 0; i < 1280; i++){
		offsetavailable[i] = 0;
	}
}

vaddr_t 
alloc_kpages(int numpages)
{	
    paddr_t paddress;
	
	if(isBooted == 0) {
		paddress = getppages(numpages);	
		if(paddress == 0){
			return 0;
		}		
	}else{
		int spl = splhigh();	

		int index = findavailablepage();

		if(index == 0){
			index = findoldestpage();
			swapout(index);
		}		
		lock_acquire(core_lock);
		
		coremap[index]->status = TRASH;
		coremap[index]->as = NULL;		
		coremap[index]->vaddress = 0;

		paddress = index * PAGE_SIZE;
		lock_release(core_lock);

		if (paddress == 0) {
		    return 0;
		}
		bzero((void*)PADDR_TO_KVADDR(index*PAGE_SIZE), PAGE_SIZE);	
		splx(spl);
	}
    return PADDR_TO_KVADDR(paddress);
}

void 
free_kpages(vaddr_t addr)
{
	paddr_t paddress = KVADDR_TO_PADDR(addr);	

	int index = paddress/PAGE_SIZE;
	int spl = splhigh();

	coremap[index]->status = FREE;
	coremap[index]->as = NULL;
	coremap[index]->vaddress = 0;
	coremap[index]->secs = 0;
	coremap[index]->nsecs = 0;


	splx(spl);
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace * as;
	vaddr_t vbase1, vtop1, vbase2, vtop2;
        int addstack = 0;
	
	// cleaning up the address (only high 20 bits required)
	faultaddress &= PAGE_FRAME;
	
	// sanity check to see if the address space is uninitialized
	as = curthread->t_vmspace;
	if (as == NULL) {
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	assert(as->as_vbase1 != 0);
	assert(as->as_npages1 != 0);
	assert(as->as_vbase2 != 0);
	assert(as->as_npages2 != 0);
	assert(as->as_stackvbase != 0);

	// collecting information about the address space
	vbase1 = (as->as_vbase1 & PAGE_FRAME);
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = (as->as_vbase2 & PAGE_FRAME);
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	 
	//check if valid user space address	
	int permission;
	if (faultaddress >= vbase1 && faultaddress < vtop1){
		permission = as->permissionv1;
	}else if (faultaddress >= vbase2 && faultaddress < vtop2){
		permission = as->permissionv2;
	}else if (faultaddress >= as->as_stackvbase && faultaddress < USERSTACK){
		permission = 7;
	}else if (faultaddress >= as->as_heapStart && faultaddress < as->as_heapEnd){
		permission = 7;
	}else if (faultaddress >= 0x70000000 && faultaddress < USERSTACK){
        addstack = (as->as_stackvbase & PAGE_FRAME)-faultaddress;
        as->as_stackvbase = faultaddress;
        as->as_stacknPages = as->as_stacknPages + addstack/PAGE_SIZE;
        permission = 7;
    }else{
        return EFAULT;
	}

	if(((permission & 2) == 0) && (faulttype == VM_FAULT_WRITE)){	
        return EFAULT;
	}
	
	lock_acquire(page_lock);

	int found = 0;
	u_int32_t entryhi = faultaddress;
	u_int32_t entrylo;

	if(faulttype == VM_FAULT_READ){
		struct ptentry * tempentry;
		int index;
		paddr_t paddress;		
		
		tempentry = findentry(as, faultaddress);
		
		if(tempentry != NULL){
			assert(tempentry->count > 0);
			found = 1;
			if(tempentry->ondisk == 1){
				index = findavailablepage();
				if(index == 0){
					index = findoldestpage();
					swapout(index);
				}	
				swapin(tempentry, index);
			}
		}else{
			index = findavailablepage();
			if(index == 0){
				index = findoldestpage();
				swapout(index);
			}
			paddress = page_alloc(as, faultaddress, index, 'v');
			tempentry = addentry(as, faultaddress, paddress);
		}

		entrylo = tempentry->paddress;
		entrylo |= TLBLO_VALID;
		entrylo &= (~TLBLO_DIRTY);
					
		assert(tempentry->paddress != 0);
        assert(faultaddress < 0x80000000);		
        
		int result = TLB_Probe(faultaddress, 0);
		if(result < 0){
			entryhi = faultaddress;
			int spl = splhigh();	
			TLB_Random(entryhi, entrylo);	
			splx(spl);
		}else {
			entryhi = faultaddress;
			int spl = splhigh();	
			TLB_Write(entryhi, entrylo, result);	
			splx(spl);
		}

	}else if(faulttype == VM_FAULT_WRITE){
		struct ptentry * tempentry;
		int index;
		paddr_t paddress;				

		tempentry = findentry(as, faultaddress);
		
		if(tempentry != NULL){
			assert(tempentry->count > 0);
			found = 1;
			
			if(tempentry->count == 1){
				if(tempentry->ondisk == 1){
					index = findavailablepage();
					if(index == 0){
						index = findoldestpage();
						swapout(index);
					}
					swapin(tempentry, index);

					entrylo = tempentry->paddress;
					entrylo |= TLBLO_VALID;
					entrylo |= TLBLO_DIRTY;					
				}else{
					entrylo = tempentry->paddress;
					entrylo |= TLBLO_VALID;
					entrylo |= TLBLO_DIRTY;
				}
			}else{
				if(tempentry->ondisk == 1){
					index = findavailablepage();
					if(index == 0){
						index = findoldestpage();
						swapout(index);
					}
					paddress = page_alloc(as, faultaddress, index, 'v');				
					tempentry = swapentry(as, tempentry, paddress);	
					swapin(tempentry, index);

					entrylo = tempentry->paddress;
					entrylo |= (TLBLO_VALID);
					entrylo &= (~TLBLO_DIRTY);
				}else {
					index = findavailablepage();
					if(index == 0){
						index = findoldestpage();
						swapout(index);
					}
					paddress = page_alloc(as, faultaddress, index, 'v');				
					tempentry = copyentry(as, tempentry, paddress);	
					
					entrylo = tempentry->paddress;
					entrylo |= (TLBLO_VALID);
					entrylo &= (~TLBLO_DIRTY);
				}			
			}
		}else if (found == 0){
			index = findavailablepage();
			if(index == 0){
				index = findoldestpage();
				swapout(index);
			}
			paddress = page_alloc(as, faultaddress, index, 'v');
			tempentry = addentry(as, faultaddress, paddress);
			entrylo = tempentry->paddress;
			entrylo |= (TLBLO_VALID);
			entrylo &= (~TLBLO_DIRTY);	
		}	
		
		assert(tempentry->paddress != 0);
        assert(faultaddress < 0x80000000);		
        
		int spl = splhigh();
		int result = TLB_Probe(faultaddress, 0);
		if(result < 0){
			entryhi = faultaddress;	
			TLB_Random(entryhi, entrylo);	
		}else {
			entryhi = faultaddress;	
			TLB_Write(entryhi, entrylo, result);	
		}
		splx(spl);
	}else if(faulttype == VM_FAULT_READONLY){
		struct ptentry * tempentry;
		int  result;         
		paddr_t paddress;
		vaddr_t vaddress;
				
		tempentry = findentry(as, faultaddress);
		if(tempentry != NULL){
			assert(tempentry->count > 0);
			found = 1;

			if(((permission & 2) >> 1) == 1){			
				int spl = splhigh();				
				if(tempentry->count == 1){
					result = TLB_Probe(faultaddress, 0);
					if(result < 0){
						entrylo = tempentry->paddress;						
						entrylo |= (TLBLO_VALID);
						entrylo |= (TLBLO_DIRTY);
						TLB_Random(faultaddress, entrylo);
					}else{
						TLB_Read(&vaddress, &paddress, result);				
						paddress |= (TLBLO_DIRTY);		
						TLB_Write(vaddress, paddress, result);
					}	
				}else{
					int index = findavailablepage();
					if(index == 0){
						index = findoldestpage();
						swapout(index);
					}
					paddress = page_alloc(as, faultaddress, index, 'v');				
					tempentry = copyentry(as, tempentry, paddress);						

					result = TLB_Probe(faultaddress, 0);
					if(result < 0){
						entrylo = paddress;						
						entrylo |= (TLBLO_VALID);
						entrylo |= (TLBLO_DIRTY);						
						TLB_Random(vaddress, entrylo);
					}else{
						TLB_Read(&vaddress, &paddress, result);				
						entrylo = paddress;						
						entrylo |= (TLBLO_VALID);					
						entrylo |= (TLBLO_DIRTY);						
						TLB_Write(vaddress, entrylo, result);
					}	
				}			
				splx(spl);
			}else{
				lock_release(core_lock);
		        panic("\ni can't take this course anymore, just kill me already\n");
			} 
		}

	}
	int spl = splhigh();
	lock_release(page_lock);

	if(found == 1){
		splx(spl);
		return 0;
	}

	if(!found){
        int result, offset, memsize, filesize;
        result = as_prepare_load(curthread->t_vmspace);
        if(result){
            splx(spl);
            return result;
        }
        if(faultaddress >= 0x400000 && faultaddress < 0x10000000){
            
            struct vnode *v;
            result = vfs_open(curthread->t_name, O_RDONLY, &v);
            if(result){
                splx(spl);
                return result;
            }

            offset = curthread->instoffset + (((faultaddress - 0x400000)/PAGE_SIZE)*PAGE_SIZE);
            if(curthread->instmemsize - (offset - curthread->instoffset) > PAGE_SIZE){
                memsize = PAGE_SIZE;
            }else{
                memsize = curthread->instmemsize-(offset - curthread->instoffset);
                if(memsize < 0){
                    memsize = 0;
                }
            }
            
            if(curthread->instfilesize - (offset - curthread->instoffset) > PAGE_SIZE){
                filesize = PAGE_SIZE;
            }else{
                filesize = curthread->instfilesize-(offset - curthread->instoffset);
                if(filesize < 0){
                    filesize = 0;
                }
            }
            
            result = load_segment(v, offset, faultaddress, memsize, filesize, curthread->instflags&1);
            if(result){
                splx(spl);
                return result;
            }
            
            vfs_close(v);            

        }else if (faultaddress >= 0x10000000 && faultaddress < as->as_heapStart){
            struct vnode *v;
            result = vfs_open(curthread->t_name, O_RDONLY, &v);
            if(result){
                splx(spl);
                return result;
            }

            offset = curthread->textoffset + (((faultaddress - 0x10000000)/PAGE_SIZE)*PAGE_SIZE);
            if(curthread->textmemsize - (offset - curthread->textoffset) > PAGE_SIZE){
                memsize = PAGE_SIZE;
            }else{
                memsize = curthread->textmemsize-(offset - curthread->textoffset);
                if(memsize < 0){
                    memsize = 0;
                }
            }
            
            if(curthread->textfilesize - (offset - curthread->textoffset) > PAGE_SIZE){
                filesize = PAGE_SIZE;
            }else{
                filesize = curthread->textfilesize-(offset - curthread->textoffset);
                if(filesize < 0){
                    filesize = 0;
                }
            }

            result = load_segment(v, offset, faultaddress, memsize, filesize, curthread->textflags&1);
            if(result){
                splx(spl);
                return result;
            }
            vfs_close(v);
        }
        result = as_complete_load(curthread->t_vmspace);
        if(result){
            splx(spl);
            return result;
        }			
	}

	splx(spl);
	return 0;
}

paddr_t 
page_alloc(struct addrspace *addrspace, vaddr_t vaddress, int index, char from){

	(void)from;
	int spl = splhigh();

	coremap[index]->as = addrspace;
	coremap[index]->vaddress = vaddress;
	coremap[index]->status = DIRTY;

	time_t secs;
	u_int32_t nsecs; 	

	gettime(&secs, &nsecs);
	coremap[index]->secs = secs;
	coremap[index]->nsecs = nsecs;
	bzero((void*)PADDR_TO_KVADDR(index*PAGE_SIZE), PAGE_SIZE);	
	splx(spl);
	return index*PAGE_SIZE;	
}

int
findavailablepage(){
	int i;
	int spl = splhigh();
	for(i = num_trash + 1; i < totalnumpages - 1; i++){
		if(coremap[i]->status == FREE){
			splx(spl);
			return i;
		}
	}

	splx(spl);
	return 0;
}

int
findoldestpage(){
	time_t secs = 1599999999;
	u_int32_t nsecs = 0;
	int i, index;

	index = -1;
	int spl = splhigh();
	for(i = num_trash + 1; i < totalnumpages - 1; i++){
		if(coremap[i]->status != TRASH){
			if(coremap[i]->secs < secs){
				secs = coremap[i]->secs;
				nsecs = coremap[i]->nsecs;
				index = i;
			}else if (coremap[i]->secs == secs && coremap[i]->nsecs < nsecs){
				nsecs = coremap[i]->nsecs;
				index = i;
			}		
		}
	}	

	if(index != -1){
		splx(spl);
		return index;
	}

	secs = 1599999999;
	nsecs = 0;
	if(index == -1){
		for(i = num_trash + 1; i < totalnumpages-1; i++){
			if(coremap[i]->secs < secs){
				secs = coremap[i]->secs;
				nsecs = coremap[i]->nsecs;
				index = i;
			}else if (coremap[i]->secs == secs && coremap[i]->nsecs < nsecs){
				nsecs = coremap[i]->nsecs;
				index = i;
			}	
		}
	}
	splx(spl);
	return index;
}












