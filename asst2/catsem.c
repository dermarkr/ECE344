/*
 * catsem.c
 *
 * Please use SEMAPHORES to solve the cat syncronization problem in 
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
#include <synch.h>
#include "catmouse.h"

/*
 * 
 * Function Definitions
 * 
 */

struct semaphore *eating;


//cronstructor for "eating" semaphore
static void initialize(char * name, int count){

        eating = sem_create(name, count); 

}

/*
 * catsem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
catsem(void * unusedpointer, 
       unsigned long catnumber)
{

        //while the current cat thread has not eaten from the bowl NMEALS times
        //set semaphore to the bowl and eat from it, then release semaphore
        //from the bowl and try again
        int iteration = 0;
        while(iteration < NMEALS){
                P(eating);
                catmouse_eat("cat", catnumber, iteration%2+1, iteration);
                iteration++;
                V(eating);
        }
        
        (void) unusedpointer;
}
        

/*
 * mousesem()
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
 *      Write and comment this function using semaphores.
 *
 */

static
void
mousesem(void * unusedpointer, 
         unsigned long mousenumber)
{

        //while the current mouse thread has not eaten from the bowl NMEALS times
        //set semaphore to the bowl and eat from it, then release semaphore
        //from the bowl and try again
        int iteration = 0;
        while(iteration < NMEALS){
                P(eating);
                catmouse_eat("mouse", mousenumber, iteration%2+1, iteration);
                iteration++;
                V(eating);
        }
        
        (void) unusedpointer;
}


/*
 * catmousesem()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catsem() and mousesem() threads.  Change this 
 *      code as necessary for your solution.
 */

int
catmousesem(int nargs,
            char ** args)
{
        int index, error;
   
        /*
         * Start NCATS catsem() threads.
         */

        //initialize semaphore
        initialize((void *)"eating", 1);

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catsem Thread", 
                                    NULL, 
                                    index, 
                                    catsem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catsem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        
        /*
         * Start NMICE mousesem() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mousesem Thread", 
                                    NULL, 
                                    index, 
                                    mousesem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mousesem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * wait until all other threads finish
         */

        while (thread_count() > 1)
                thread_yield();

        (void)nargs;
        (void)args;
        kprintf("catsem test done\n");

        return 0;
}

