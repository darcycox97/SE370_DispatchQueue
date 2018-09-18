#include "dispatchQueue.h"
#include <string.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <semaphore.h>


///////////// HELPER PROTOTYPES /////////////////
dispatch_queue_thread_t *create_new_thread(void);


task_t *task_create(void (* work)(void *), void *param, char* name) {

  task_t *task = malloc(sizeof(task_t));

  strcpy(task->name, name);
  task->work = work;
  task->params = param;

  return task;

}

void task_destroy(task_t *task) {
  free(task);
}


dispatch_queue_t *dispatch_queue_create(queue_type_t queue_type) {
  dispatch_queue_t *queue = malloc(sizeof(dispatch_queue_t));

  queue->queue_type = queue_type;
  queue->head = queue->tail = NULL;
  queue->tasks_semaphore = malloc(sizeof(sem_t));

  // init tasks_semaphore to 0 because can't dispatch until task is added
  sem_init(queue->tasks_semaphore, 0, 0);

  //TODO: set up dispatch thread that uses semaphore to determine when it can execute new tasks.
  // semaphore will be num_cores for concurrent queue, or 1 for serial

  // initialise thread pool for the queue, number of threads depends on type of queue
  thread_pool_t *thread_pool = malloc(sizeof(thread_pool_t));
  thread_pool->head = NULL;
  thread_pool->thread_pool_semaphore = malloc(sizeof(sem_t));

  if (queue_type == CONCURRENT) {
    // add num_cores threads to the thread pool and initialise semaphore
    for (int i = 0; i < get_nprocs_conf(); i++) {
       add_to_thread_pool(thread_pool, create_new_thread());
    }

    // initially N available threads
    sem_init(thread_pool->thread_pool_semaphore, 0, get_nprocs_conf());

  } else {
    // queue_type == SERIAL, only one thread in thread pool
    add_to_thread_pool(thread_pool, create_new_thread());
    sem_init(thread_pool->thread_pool_semaphore, 0, 1);
  }

  queue->threads = thread_pool;

  return queue;
}

void dispatch_queue_destroy(dispatch_queue_t *queue) {


}


int dispatch_async(dispatch_queue_t *queue, task_t *task) {
  add_to_dispatch_queue(queue, task);
  return 0;
}

int dispatch_sync(dispatch_queue_t *queue, task_t *task) {
  add_to_dispatch_queue(queue, task);
  // block until task has been completed
  sem_wait(task->task_semaphore);
  return 0;
}


/////// DISPATCH QUEUE AND THREAD POOL MANAGEMENT FUNCTIONS ////////////

void add_to_dispatch_queue(dispatch_queue_t *queue, task_t *task) {

  // add task to queue
  if (queue->head == null) {
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
  // block until there is a task on the dispatch queue 
  sem_wait(queue->tasks_semaphore);
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

dispatch_queue_thread_t *create_new_thread() {

  dispatch_queue_thread_t *thread = malloc(sizeof(dispatch_queue_thread_t));
  thread->queue = queue;
  thread->thread = malloc(sizeof(pthread_t));
  pthread_create(//TODO, function that calls sem_wait til task added)
  thread->thread_semaphore = malloc(sizeof(sem_t));
  sem_init(thread->thread_semaphore, 0, 0); // 0 because no task allocated yet
  thread->next = NULL;

  return thread;
}
