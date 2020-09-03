/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>
#include <queue.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}
        
	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}
	
        //initialize the lock to no owner and make a wait queue
        lock->isHeld = 0;
        lock->waitqueue = q_create(256);
        
	return lock;
}

void
lock_destroy(struct lock *lock)
{
        int spl;
        assert(lock != NULL);
        
        spl = splhigh();
        //while the lock is being held, add current thread to the waitqueue
        //and put the thread to sleep
        while(lock->isHeld == 1){
            q_addtail(lock->waitqueue, (void *)curthread);
            thread_sleep(curthread);   
        }   
        splx(spl);
        
        //once thread is awake, destroy the waitqueue and free up the memory
        q_destroy(lock->waitqueue);
        
	kfree(lock->name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
    int spl;
    spl = splhigh();
    
    //while the lock is being held, add current thread to the waitqueue
    //and put the thread to sleep
    while(lock->isHeld == 1){
        q_addtail(lock->waitqueue, (void *)curthread);
        thread_sleep(curthread);   
    }
    //once thread is awake, make the current thread the owner of the lock
    lock->owner = (volatile int)curthread;
    lock->isHeld = 1;
    
    splx(spl);
}

void
lock_release(struct lock *lock)
{
    int spl;
    spl = splhigh(); 
    
    //if the lock is currently being held, and the owner is the current thread
    //unlock the lock and wakeup the next thread in the waitqueue
    if(lock->isHeld == 1 && lock->owner == (volatile int)curthread){
        lock->isHeld = 0; 
        lock->owner = 0; 
        
        if(q_empty(lock->waitqueue) == 0){
            thread_wakeup((void *)q_remhead(lock->waitqueue));
        }
    }
    
    splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
    int spl;
    spl = splhigh();
    int father;
    //you're the father
    if(lock->owner == (volatile int)curthread){
        father = 1;
    }
    //you're not the father
    else{
        father = 0;
    }
    splx(spl);
    return father;
    
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
        
        cv->waitqueue = q_create(256);	
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	// add stuff here as needed
	q_destroy(cv->waitqueue);
	kfree(cv->name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    lock_release(lock);
    
    int spl;
    spl = splhigh();
    
    q_addtail(cv->waitqueue, (void *)curthread);
    thread_sleep(curthread);
    
    splx(spl);
    
    lock_acquire(lock);
    (void)cv;
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int spl;
    spl = splhigh();
    
    thread_wakeup((void *)q_remhead(cv->waitqueue));
    
    splx(spl);
    (void)lock;
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int spl;
    spl = splhigh();
    
    while(q_empty(cv->waitqueue) != 1){
        thread_wakeup((void *)q_remhead(cv->waitqueue));
    }
    
    splx(spl);
    (void)lock;
}
