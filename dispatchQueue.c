#include "dispatchQueue.h"

task_t *task_create(void (* work)(void *), void *param, char* name) {

  task_t *task = malloc(sizeof(task_t));

  task->name = name;
  task->work = work;
  task->params = param;

  return task;

}

void task_destroy(task_t *task) {
  free(task);
}
