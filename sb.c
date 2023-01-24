#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
  if (argc != 4)
  {
    printf(1, "Usage: sb pid priority_ratio arrival_ratio exec_cycle_ratio\n");
    exit();
  }

  int priority_ratio = atoi(argv[1]);
  int arrival_ratio = atoi(argv[2]);
  int exec_cycle_ratio = atoi(argv[3]);
  set_bjf(priority_ratio, arrival_ratio, exec_cycle_ratio);
  exit();
}