/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>
#include <syscall.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, char **args)
{
    int i, result, argnum;
    result = 0;
    argnum = 0;
    /*
     1. delete current thread address space 
     2. create new address space, call load_elf to store program into address space 
     3. do the stack stuff from thread_fork and the error checking shit 
     4. call md_initpcb at main?
     5. make it runnable 
     6. transfer to user mode
     */

    while(args[argnum] != NULL){
        argnum++;
    } 
    
    if(argnum == 0 || progname == NULL){
        return EFAULT;
    }
    
    struct vnode *v;

    result = vfs_open((char *)curthread->t_name, O_RDONLY, &v);
    if (result) {
        return result;
    }
	
    /* We should be a new thread. */
    assert(curthread->t_vmspace == NULL);
    
    /* Create a new address space. */
    curthread->t_vmspace = as_create();
    if (curthread->t_vmspace==NULL) {
        vfs_close(v);
        return ENOMEM;
    }
    
    /* Activate it. */
    as_activate(curthread->t_vmspace);

    vaddr_t entrypoint, stackptr;
    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* thread_exit destroys curthread->t_vmspace */
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */   
    result = as_define_stack(curthread->t_vmspace, &stackptr);
    if (result) {
        /* thread_exit destroys curthread->t_vmspace */
        return result;
    }
    
    char ** argptr = (char **)kmalloc(sizeof(char *)*(argnum+1));
    argptr[argnum] = NULL;

    char ** argtemp = (char**)kmalloc(sizeof(char*)*argnum+1); 

    if(argnum > 2){
        for(i = 0; i < argnum - 1; i++){
            argtemp[i] = args[i];
        }

        argtemp[argnum - 1] = NULL;
        argnum = argnum - 2;
    }
    
    for(i = 0; i < argnum; i++){
        int templen = strlen(args[i]) + 1;
        stackptr -= templen;
        if(stackptr%4 != 0){
            stackptr -= stackptr%4;
        }

        char * toCopy;

        if(argnum < 3){
            toCopy = args[i];
        }else{
            toCopy = argtemp[i];
        }
        result = copyout((const void *)toCopy, (userptr_t)stackptr, (size_t)templen);
        if(result != 0){
            return EFAULT;
        }
        argptr[i] = (char*)stackptr;
    }
	
    stackptr -= (sizeof(char *)*(argnum+1));
    result = copyout((const void*)argptr, (userptr_t)stackptr, (size_t)(sizeof(char *)*(argnum+1)));
    
    kfree(argtemp);
    kfree(argptr);

    /* Warp to user mode. */
    md_usermode(argnum, (userptr_t)stackptr, stackptr, entrypoint);
    
    return 0;
}
    
