#include "dispatchQueue.h"
#include <string.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>

///////////// HELPER PROTOTYPES /////////////////
dispatch_queue_thread_t *create_new_thread(dispatch_queue_t*);
void worker_thread_start(void *);
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
    dispatch_queue_t *queue = (dispatch_queue_t *)malloc(sizeof(dispatch_queue_t));

    queue->is_waited_on = 0;
    queue->queue_type = queue_type;
    queue->head = queue->tail = NULL;
    queue->tasks_semaphore = (sem_t *)malloc(sizeof(sem_t));
    queue->completion_semaphore = (sem_t *) malloc(sizeof(sem_t));


    // init tasks_semaphore to 0 because can't dispatch until task is added
    // same for completion_semaphore, which will unlock when the queue finishes
    sem_init(queue->tasks_semaphore, 0, 0);
    sem_init(queue->completion_semaphore, 0, 0);

    // initialise thread pool for the queue, number of threads depends on type of queue
    thread_pool_t *thread_pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    thread_pool->head = NULL;
    thread_pool->thread_pool_semaphore = (sem_t *)malloc(sizeof(sem_t));

    if (queue_type == CONCURRENT) {
        // add num_cores threads to the thread pool and initialise semaphore
        for (int i = 0; i < get_nprocs_conf(); i++) {
            add_to_thread_pool(thread_pool, create_new_thread(queue));
        }

        // initially N available threads
        sem_init(thread_pool->thread_pool_semaphore, 0, (unsigned int) get_nprocs_conf());

    } else {
        // queue_type == SERIAL, only one thread in thread pool
        add_to_thread_pool(thread_pool, create_new_thread(queue));
        sem_init(thread_pool->thread_pool_semaphore, 0, 1);
    }

    queue->threads = thread_pool;

    // set up dispatcher thread whose job is to detect when new tasks are available , and assign them to worker threads
    pthread_t *dispatcher_thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(dispatcher_thread, NULL, (void *) dispatcher_thread_start, (void *) queue);

    return queue;
}

void dispatch_queue_destroy(dispatch_queue_t *queue) {
    //TODO
}

void dispatch_for(dispatch_queue_t *queue, long n, void (*work)(long)) {
    //TODO
}

int dispatch_queue_wait(dispatch_queue_t *queue) {
    queue->is_waited_on = 1; // tells the queue to not accept any more tasks.
    
    while(1) {
        // wait until all tasks in the queue have finished executing
        // this means the queue is empty and all threads are returned to the thread pool

        // get number of tasks in queue
        int num_tasks_left;
        sem_getvalue(queue->tasks_semaphore, &num_tasks_left);

        // get number of threads in pool and determine if pool is full
        int num_threads_in_pool;
        sem_getvalue(queue->threads->thread_pool_semaphore, &num_threads_in_pool);
        int thread_pool_full;
        if (queue->queue_type == SERIAL) {
            thread_pool_full = num_threads_in_pool == 1;
        } else {
            thread_pool_full = num_threads_in_pool == get_nprocs_conf();
        }

        if (thread_pool_full && num_tasks_left == 0) {
            break;
        }
    }
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
    // block until task has been completed
    sem_wait(task->task_semaphore);
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
    pthread_create(thread->thread, NULL, (void *)worker_thread_start, (void *)thread);
    thread->thread_semaphore = (sem_t *)malloc(sizeof(sem_t));
    sem_init(thread->thread_semaphore, 0, 0); // 0 because no task allocated yet
    thread->next = NULL;

    return thread;
}

// starts a worker thread which will be sitting in a thread pool. Whenever assigned a task,
// the worker thread will run that task and will wait until a new task is assigned.
void worker_thread_start(void * dispatch_queue_thread) {

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

        // if task was added asynchronously, destroy it. Otherwise the code waiting for the task to complete will
        // destroy it once it detects the semaphore has unlocked.
        if (queue_thread->task->type == ASYNC) {
            task_destroy(queue_thread->task);
        }

        queue_thread->task = NULL;
    }

}

void dispatcher_thread_start(void * dispatch_queue) {
    dispatch_queue_t *queue = (dispatch_queue_t *) dispatch_queue;

    while(1) {
        // wait for tasks to be ready for execution
        sem_wait(queue->tasks_semaphore);
        // once we have a task, wait for an available thread
        sem_wait(queue->threads->thread_pool_semaphore);

        // get the task that is next in the queue and assign it to a worker thread from the queue's thread pool
        task_t *task = remove_from_dispatch_queue(queue);
        dispatch_queue_thread_t *thread = remove_from_thread_pool(queue->threads);
        thread->task = task;
        sem_post(thread->thread_semaphore); // unlock the thread's semaphore so it knows a task is now assigned
    }
}