#include <stdio.h>
#include <sys/sysinfo.h>

int main() {
  printf("Number of cores: %d\n", get_nprocs_conf());
  return 0;
}
