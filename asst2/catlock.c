/*
 * catlock.c
 *
 * Please use LOCKS/CV'S to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include "catmouse.h"
#include <synch.h>

/*
 * 
 * Function Definitions
 * 
 */

struct lock * checkbowl;

static
void
initialize(){
    checkbowl = lock_create("checkbowl");
}

static
void
terminate(){
    lock_destroy(checkbowl);
}

/*
 * catlock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS -
 *      1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
catlock(void * unusedpointer, 
        unsigned long catnumber)
{
        /*
         * Avoid unused variable warnings.
         */
    
        //while the current cat thread has not eaten from the bowl NMEALS times
        //acquire the lock to the bowl and eat from it, then release the lock
        //from the bowl and try again
        int iteration = 0;
        while(iteration < NMEALS){
            lock_acquire(checkbowl);
                catmouse_eat("cat", catnumber, iteration%2+1, iteration);
                iteration++;
            lock_release(checkbowl);
        }
        
        (void) unusedpointer;
        (void) catnumber;
}
	

/*
 * mouselock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
mouselock(void * unusedpointer,
          unsigned long mousenumber)
{   
        /*
         * Avoid unused variable warnings.
         */
         
        //while the current mouse thread has not eaten from the bowl NMEALS times
        //acquire the lock to the bowl and eat from it, then release the lock
        //from the bowl and try again
        int iteration = 0;
        while(iteration < NMEALS){
            lock_acquire(checkbowl);
            catmouse_eat("mouse", mousenumber, iteration%2+1, iteration);
            iteration++;
            lock_release(checkbowl);
        }
        
        (void) unusedpointer;
        (void) mousenumber;
}


/*
 * catmouselock()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catlock() and mouselock() threads.  Change
 *      this code as necessary for your solution.
 */

int
catmouselock(int nargs,
             char ** args)
{
        int index, error;
        
        //initialize locks
        initialize();
        
        /*
         * Start NCATS catlock() threads.
         */

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catlock thread", 
                                    NULL, 
                                    index, 
                                    catlock, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catlock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * Start NMICE mouselock() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mouselock thread", 
                                    NULL, 
                                    index, 
                                    mouselock, 
                                    NULL
                                    );
      
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mouselock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        
        /*
         * wait until all other threads finish
         */
        
        while (thread_count() > 1)
                thread_yield();
        
        //destroy locks
        terminate();
        (void)nargs;
        (void)args;
        kprintf("catlock test done\n");

        return 0;
}

