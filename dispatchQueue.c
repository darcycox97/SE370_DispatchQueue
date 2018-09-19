#include "dispatchQueue.h"
#include <string.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>

///////////// HELPER PROTOTYPES /////////////////
dispatch_queue_thread_t *create_new_thread(dispatch_queue_t*);
void concurrent_worker_thread_start(void *);
void dispatcher_thread_start(void *);
/////////////////////////////////////////////////

//////// IMPLEMENTATIONS OF FUNCTIONS DEFINED IN THE HEADER/ SPECIFIED BY THE ASSIGNMENT STARTER CODE //////////

task_t *task_create(void (* work)(void *), void *param, char* name) {

    task_t *task = (task_t *)malloc(sizeof(task_t));

    strcpy(task->name, name);
    task->work = work;
    task->params = param;
    task->next = NULL;
    task->task_semaphore = (sem_t *)malloc(sizeof(sem_t));
    sem_init(task->task_semaphore, 0, 0); // is unlocked when task is completed

    return task;
}

void task_destroy(task_t *task) {
    // need to free the task_semaphore because it was created by the task_create function
    // don't need to free other members because they weren't created by our code so they may be needed in calling code
    free(task->task_semaphore);
    free(task);
}


dispatch_queue_t *dispatch_queue_create(queue_type_t queue_type) {
    // allocate memory for the queue
    dispatch_queue_t *queue = (dispatch_queue_t *)malloc(sizeof(dispatch_queue_t));

    queue->is_waited_on = 0;
    queue->queue_type = queue_type;
    queue->head = queue->tail = NULL;
    queue->tasks_semaphore = (sem_t *)malloc(sizeof(sem_t));
    queue->queue_empty_semaphore = (sem_t *) malloc(sizeof(sem_t));
    queue->threads_free_semaphore = (sem_t *) malloc(sizeof(sem_t));


    // init tasks_semaphore to 0 because can't dispatch until task is added
    // same for queue_empty_semaphore, which will unlock when the queue becomes empty
    // as for threads_free_semaphore which unlocks when all threads are available
    sem_init(queue->tasks_semaphore, 0, 0);
    sem_init(queue->queue_empty_semaphore, 0, 0);
    sem_init(queue->threads_free_semaphore, 0, 0);

    // initialise thread pool with num_cores threads if queue is concurrent
    if (queue_type == CONCURRENT) {

        thread_pool_t *thread_pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
        thread_pool->head = NULL;
        thread_pool->thread_pool_semaphore = (sem_t *)malloc(sizeof(sem_t));

        // add num_cores threads to the thread pool and initialise semaphore
        for (int i = 0; i < get_nprocs_conf(); i++) {
            add_to_thread_pool(thread_pool, create_new_thread(queue));
        }

        // initially N available threads
        sem_init(thread_pool->thread_pool_semaphore, 0, (unsigned int) get_nprocs_conf());
        queue->threads = thread_pool;
    } else {
        queue->threads = NULL; // a serial queue has no thread pool
    }


    // set up dispatcher thread whose job is to detect when new tasks are available and assign them to worker threads
    // or just to execute the tasks if it is a serial queue
    pthread_t *dispatcher_thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(dispatcher_thread, NULL, (void *) dispatcher_thread_start, (void *) queue);

    return queue;
}

void dispatch_queue_destroy(dispatch_queue_t *queue) {
    //TODO
}

void dispatch_for(dispatch_queue_t *queue, long n, void (*work)(long)) {
    
    //TODO: 

    // execute the work function n times, with the argument going from 0 to n - 1.
    // return from the functon once all function calls have finished

    for (long i = 0; i < n; i++) {
        // create new pointer for argument of each created task to avoid concurrency issues with 
        // tasks accessing the same memory address as the loop counter.
        long parameter = i;

        task_t *task = task_create((void (*) (void *)) work, &parameter, "task");

        dispatch_async(queue, task);
    }

    dispatch_queue_wait(queue);
}

int dispatch_queue_wait(dispatch_queue_t *queue) {
    queue->is_waited_on = 1; // tells the queue to not accept any more tasks.

    // wait for the queue to empty
    sem_wait(queue->queue_empty_semaphore);
    // once queue has emptied, theads could (most likely will) still be executing
    // so wait for all threads to finish their work
    sem_wait(queue->threads_free_semaphore);

    return 0;
}

int dispatch_async(dispatch_queue_t *queue, task_t *task) {
    task->type = ASYNC;
    add_to_dispatch_queue(queue, task);
    return 0;
}

int dispatch_sync(dispatch_queue_t *queue, task_t *task) {
    task->type = SYNC;
    add_to_dispatch_queue(queue, task);
    sem_wait(task->task_semaphore);  // block until task has been completed
    task_destroy(task); // free up memory used by the task
    return 0;
}


/////// DISPATCH QUEUE AND THREAD POOL MANAGEMENT FUNCTIONS ////////////

void add_to_dispatch_queue(dispatch_queue_t *queue, task_t *task) {

    if (queue->is_waited_on) {
        return; // we ignore any subsequent attempts to dispatch tasks after a call to dispatch_queue_wait
    }

    // add task to queue
    if (queue->head == NULL) {
        // if the queue is empty, the task should be added as the head and tail of the queue
        queue->head = task;
        queue->tail = task;
    } else {
        // else the task should just be added on to the end of the queue
        queue->tail->next = task; // point current end of queue to added task
        queue->tail = task;  // update tail to be added task
    }

    task->next = NULL;

    // increment semaphore because new task is available for execution
    sem_post(queue->tasks_semaphore);
}

task_t *remove_from_dispatch_queue(dispatch_queue_t *queue) {
    task_t *task = queue->head;
    queue->head = task->next;  // ensure head now points to the next task in the queue
    return task;
}

void add_to_thread_pool(thread_pool_t *thread_pool, dispatch_queue_thread_t *thread) {

    if (thread_pool->head != NULL) {
        thread->next = thread_pool->head;
    } else {
        thread->next = NULL;
    }

    thread_pool->head = thread;
}

dispatch_queue_thread_t *remove_from_thread_pool(thread_pool_t *thread_pool) {

    dispatch_queue_thread_t *thread = thread_pool->head;
    thread_pool->head = thread_pool->head->next;

    return thread;
}


//////////////// HELPERS //////////////////

// initialises worker thread and runs it. Continuously checks for an assigned task and runs it when assigned
dispatch_queue_thread_t *create_new_thread(dispatch_queue_t *queue) {

    dispatch_queue_thread_t *thread = (dispatch_queue_thread_t *)malloc(sizeof(dispatch_queue_thread_t));
    thread->queue = queue;
    thread->thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(thread->thread, NULL, (void *)concurrent_worker_thread_start, (void *)thread);
    thread->thread_semaphore = (sem_t *)malloc(sizeof(sem_t));
    sem_init(thread->thread_semaphore, 0, 0); // 0 because no task allocated yet
    thread->next = NULL;

    return thread;
}

// starts a worker thread which will be sitting in a thread pool of a concurrent queue. Whenever assigned a task,
// the worker thread will run that task and will wait until a new task is assigned.
void concurrent_worker_thread_start(void * dispatch_queue_thread) {

    dispatch_queue_thread_t *queue_thread = (dispatch_queue_thread_t *)dispatch_queue_thread;
    while(1) {
        // wait until task is assigned to the dispatch_queue_thread
        sem_wait(queue_thread->thread_semaphore);

        // call the work function with its parameters
        (queue_thread->task->work)(queue_thread->task->params);

        // update semaphore so dispatcher thread knows another thread is available in the thread pool and
        // add this thread back into the thread pool
        add_to_thread_pool(queue_thread->queue->threads, queue_thread);
        sem_post(queue_thread->queue->threads->thread_pool_semaphore);

        sem_post(queue_thread->task->task_semaphore); // unlock task's semaphore so we know it is finished

        // unlock threads free semaphore if number of available threads is the number of cores
        // and if the queue is being waited on
        if (queue_thread->queue->is_waited_on) {
            int num_free_threads;
            sem_getvalue(queue_thread->queue->threads->thread_pool_semaphore, &num_free_threads);

            if (num_free_threads == get_nprocs_conf()) {
                sem_post(queue_thread->queue->threads_free_semaphore);
            }
        }

        // if task was added asynchronously, destroy it. Otherwise the code waiting for the task to complete will
        // destroy it once it detects the semaphore has unlocked.
        if (queue_thread->task->type == ASYNC) {
            task_destroy(queue_thread->task);
        }
    }

}

// function that the dispatcher thread uses. Waits for tasks to arrive on the queue and if the 
// queue is concurrent, assigns them to threads from the thread pool. If the queue is serial,
// there is no thread pool as we only want to use one thread, so the dispatcher thread does the work.
void dispatcher_thread_start(void * dispatch_queue) {
    dispatch_queue_t *queue = (dispatch_queue_t *) dispatch_queue;

    while(1) {
        // wait for tasks to be ready for execution
        sem_wait(queue->tasks_semaphore);

        // get the task that is next in the queue
        task_t *task = remove_from_dispatch_queue(queue);

        if (queue->queue_type == SERIAL) {
            // do the work
            (task->work)(task->params); 
            sem_post(task->task_semaphore); // notify waiting code that task is complete

            if (queue->is_waited_on) {
                // unlock the threads_free_semaphore if some code is waiting on the queue to finish
                // because the thread has just finished its work so is "free"
                sem_post(queue->threads_free_semaphore);
            }

            // destroy the task if it was dispatched asynchronously
            // otherwise the code wiating on the task will destroy it
            if (task->type == ASYNC) {
                task_destroy(task);
            }

        } else {
            // queue type is CONCURRENT
            // once we have a task, wait for an available thread
            sem_wait(queue->threads->thread_pool_semaphore);
            
            // assign task to an available thread
            dispatch_queue_thread_t *thread = remove_from_thread_pool(queue->threads);
            thread->task = task;
            sem_post(thread->thread_semaphore); // unlock the thread's semaphore so it knows a task is now assigned
        }

        // if queue is waited on and is empty, unlock the queue_empty_semaphore.
        // we do this AFTER assigning the task to a thread to avoid the small window where 
        // all threads are available and the queue is empty because the task that was extracted hasn't yet 
        // been assigned. If this was unlocked before the task was assigned, the queue_wait function might 
        // sneak past the sem_wait(threads_free) call and the calling code would terminate too early.
        if (queue->head == NULL && queue->is_waited_on) {
            sem_post(queue->queue_empty_semaphore);
        }
    }
}