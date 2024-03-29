/* 
 * File:   dispatchQueue.h
 * Author: robert
 *
 * Modified by: dcox740
 */

#ifndef DISPATCHQUEUE_H
#define	DISPATCHQUEUE_H

#include <pthread.h>
#include <semaphore.h>
    
#define error_exit(MESSAGE)     perror(MESSAGE), exit(EXIT_FAILURE)

    typedef enum { // whether dispatching a task synchronously or asynchronously
        ASYNC, SYNC
    } task_dispatch_type_t;
    
    typedef enum { // The type of dispatch queue.
        CONCURRENT, SERIAL
    } queue_type_t;

    typedef struct task task_t;

    struct task {
        char name[64];              // to identify it when debugging
        void (*work)(void *);       // the function to perform
        void *params;               // parameters to pass to the function
        task_dispatch_type_t type;  // asynchronous or synchronous
        task_t *next;	   	    // points to the next task in the queue
        sem_t *task_semaphore;      // so we know when this task is finished
    };

    typedef struct dispatch_queue_t dispatch_queue_t; // the dispatch queue type
    typedef struct dispatch_queue_thread_t dispatch_queue_thread_t; // the dispatch queue thread type

    struct dispatch_queue_thread_t {
        dispatch_queue_t *queue;// the queue this thread is associated with
        pthread_t *thread;       // the thread which runs the task
        sem_t *thread_semaphore; // the semaphore the thread waits on until a task is allocated
        task_t *task;           // the current task for this thread
        dispatch_queue_thread_t *next; // pointer to next available thread in pool
    };

    typedef struct thread_pool {
        sem_t *thread_pool_semaphore; // waits on this for threads to become available
        dispatch_queue_thread_t *head; // points to the next available thread
    } thread_pool_t;

    struct dispatch_queue_t {
        queue_type_t queue_type;  // the type of queue - serial or concurrent
        task_t *head;
        task_t *tail;
	    thread_pool_t *threads;  // the threads associated with this queue
        sem_t *tasks_semaphore; // keeps track of the number of tasks ready to be executed
        int is_waited_on; // used to check if queue_wait has been called for this queue.
        // the next two members are used in conjuction to wait for all tasks in a queue to finish
        sem_t *queue_empty_semaphore; // used so callers can wait for this queue to empty
        sem_t *threads_free_semaphore; // callers can wait for threads to become free
        pthread_t *dispatcher_thread; // the thread that accesses the queue
    };

    //////// PROTOTYPES FOR THREAD POOL AND DISPATCH QUEUE MANAGEMENT ///////

    void add_to_thread_pool(thread_pool_t*, dispatch_queue_thread_t*);

    dispatch_queue_thread_t *remove_from_thread_pool(thread_pool_t*);

    void add_to_dispatch_queue(dispatch_queue_t*, task_t*);

    task_t *remove_from_dispatch_queue(dispatch_queue_t*);


    ///////// PROTOTYPES PROVIDED BY ASSIGNMENT //////////////////

    task_t *task_create(void (*)(void *), void *, char*);
    
    void task_destroy(task_t *);

    dispatch_queue_t *dispatch_queue_create(queue_type_t);
    
    void dispatch_queue_destroy(dispatch_queue_t *);
    
    int dispatch_async(dispatch_queue_t *, task_t *);
    
    int dispatch_sync(dispatch_queue_t *, task_t *);
    
    void dispatch_for(dispatch_queue_t *, long, void (*)(long));
    
    int dispatch_queue_wait(dispatch_queue_t *);

#endif	/* DISPATCHQUEUE_H */
