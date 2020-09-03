/* 
 * stoplight.c
 *
 * You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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


/*
 * Number of cars created.
 */

#define NCARS 20


/*
 *
 * Function Definitions
 *
 */

struct semaphore* carcheck;

struct semaphore * ne; 
struct semaphore * nw; 
struct semaphore * se;
struct semaphore * sw; 

static const char *directions[] = { "N", "E", "S", "W" };
static const char *turns[] = {"NE","NW","SW","SE"};

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };

static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}
 
static void 
initialize(){
    carcheck = sem_create("carcheck", 3);
    ne = sem_create("ne", 1);
    nw = sem_create("nw", 1);
    se = sem_create("se", 1);
    sw = sem_create("sw", 1);
}

static void
terminate(){
    sem_destroy(carcheck);
    sem_destroy(ne);
    sem_destroy(nw);
    sem_destroy(se);
    sem_destroy(sw);
}


/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{
        /*
         * Avoid unused variable warnings.
         */
    struct semaphore * nextstep;
    struct semaphore * prevstep;
    int destination [3]; 
    int stepcount = 0;
    
    //find the required intersection semaphores to reach destination
    if(strcmp(directions[cardirection], "N") == 0){
        destination[0] = 1;
        destination[1] = 2;
        destination[2] = 2;
    }else if(strcmp(directions[cardirection], "S") == 0){
        destination[0] = 3;
        destination[1] = 0;
        destination[2] = 0;
    }else if(strcmp(directions[cardirection], "E") == 0){
        destination[0] = 0;
        destination[1] = 1;
        destination[2] = 3;
    }else if(strcmp(directions[cardirection], "W") == 0){
        destination[0] = 2;
        destination[1] = 3;
        destination[2] = 1;
    }
    
    //message that the car is approaching the intersection
    int finaldest = destination[2];
    message(0, carnumber, cardirection, finaldest);
    
    //iteration through the required intersection semaphores
    while(stepcount < 2){
        int index = destination[stepcount];
        if(strcmp(turns[index], "NE") == 0){
            nextstep = ne; 
        }else if(strcmp(turns[index], "NW") == 0){
            nextstep = nw;
        }else if(strcmp(turns[index], "SW") == 0){
            nextstep = sw;
        }else if(strcmp(turns[index], "SE") == 0){
            nextstep = se;
        }
        
        //make sure to acquire the next step's semaphore before releasing the
        //current step's semaphore
        P(nextstep);
        
        message(stepcount+1, carnumber, cardirection, finaldest);
        
        if(stepcount != 0){
            V(prevstep);
        }
        
        prevstep = nextstep;
        stepcount++;
    }
    //message that the car is leaving the intersection and release the semaphore
    message(4, carnumber, cardirection, finaldest);
    V(prevstep);
}


/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{
        /*
         * Avoid unused variable warnings.
         */

    struct semaphore * nextstep;
    struct semaphore * prevstep;
    int destination [4]; 
    int stepcount = 0;
    
    //find the required intersection semaphores to reach destination
    if(strcmp(directions[cardirection], "N") == 0){
        destination[0] = 1;
        destination[1] = 2;
        destination[2] = 3;
        destination[3] = 1;
    }else if(strcmp(directions[cardirection], "S") == 0){
        destination[0] = 3;
        destination[1] = 0;
        destination[2] = 1;
        destination[3] = 3;
    }else if(strcmp(directions[cardirection], "E") == 0){
        destination[0] = 0;
        destination[1] = 1;
        destination[2] = 2;
        destination[3] = 2;
    }else if(strcmp(directions[cardirection], "W") == 0){
        destination[0] = 2;
        destination[1] = 3;
        destination[2] = 0;
        destination[3] = 0;
    }
    
    //message that the car is approaching intersection
    int finaldest = destination[3];
    message(0, carnumber, cardirection, finaldest);
    
    //iterate through the required intersection semaphores
    while(stepcount < 3){
        int index = destination[stepcount];
        if(strcmp(turns[index], "NE") == 0){
            nextstep = ne; 
        }else if(strcmp(turns[index], "NW") == 0){
            nextstep = nw;
        }else if(strcmp(turns[index], "SW") == 0){
            nextstep = sw;
        }else if(strcmp(turns[index], "SE") == 0){
            nextstep = se;
        }
        
        //make sure to acquire the next step's semaphore before releaseing 
        //the current step's semaphore
        P(nextstep);
        
        message(stepcount+1, carnumber, cardirection, finaldest);
        
        if(stepcount != 0){
            V(prevstep);
        }
        prevstep = nextstep;
        
        stepcount++;
    }
    //message that the car is leaving the intersection and return semaphore
    message(4, carnumber, cardirection, finaldest);
    V(prevstep);

}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{
        /*
         * Avoid unused variable warnings.
         */
    struct semaphore * nextstep;
    int destination; 
    
    //find the destination of the car and the semaphore required to get there
    if(strcmp(directions[cardirection], "N") == 0){
        nextstep = nw;
        destination = 3;
        //destination[1] = 3; 
    }else if(strcmp(directions[cardirection], "S") == 0){
        nextstep = se;
        destination = 1;
        //destination[1] = 1;
    }else if(strcmp(directions[cardirection], "E") == 0){
        nextstep = ne;
        destination = 0;
        //destination[1] = 0;
    }else if(strcmp(directions[cardirection], "W") == 0){
        nextstep = sw;
        destination = 2;
        //destination[1] = 2;
    }
    
    //message that the car is approaching
    message(0, carnumber, cardirection, destination);
    
    //acquire the required intersection semaphore
    P(nextstep); 
    
    //once required intersection semaphore is acquired, message that the car
    //is moving into region 1 and then immediately message that the car is leaving
    message(1, carnumber, cardirection, destination); 
    message(4, carnumber, cardirection, destination);
    
    //return the intersection semaphore
    V(nextstep);
    
}


/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */
 
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
        int cardirection;
        int turndirection;
        /*
         * Avoid unused variable and function warnings.
         */
        (void) unusedpointer;
        /*
         * cardirection is set randomly.
         */
        
        //only 3 cars can enter the intersection at a time to prevent deadlocks
        //each car must acquire a sempahore before approaching the intersection
        P(carcheck);
        
        //after car is able to approach intersection, assign random direction and turn
        cardirection = random() % 4;
        turndirection = random() % 3; 
        if(turndirection == 0){
            turnright(cardirection, carnumber);
        }else if (turndirection == 1){
            gostraight(cardirection, carnumber);
        }else if (turndirection == 2){
            turnleft(cardirection, carnumber);
        }
        
        //release the semaphore when car has finished turn
        V(carcheck);
}


/*
 * createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;
        //initialize the semaphores
        initialize();
        
        /*
         * Start NCARS approachintersection() threads.
         */

        for (index = 0; index < NCARS; index++) {
                error = thread_fork("approachintersection thread",
                                    NULL, index, approachintersection, NULL);

                /*
                * panic() on error.
                */

                if (error) {         
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error));
                }
        }
        
        /*
         * wait until all other threads finish
         */

        while (thread_count() > 1)
                thread_yield();
        
        //destroy the semaphores
        terminate();
	(void)message;
        (void)nargs;
        (void)args;
        kprintf("stoplight test done\n");
        return 0;
}

