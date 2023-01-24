#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
  if (argc != 5)
  {
    printf(1, "Usage: sbp <pid, priority_ratio, arrival_ratio, exec_cycle_ratio>\n");
    exit();
  }

  int pid = atoi(argv[1]);
  int priority_ratio = atoi(argv[2]);
  int arrival_ratio = atoi(argv[3]);
  int exec_cycle_ratio = atoi(argv[4]);
  set_bjf_process(pid, priority_ratio, arrival_ratio, exec_cycle_ratio);
  exit();
}