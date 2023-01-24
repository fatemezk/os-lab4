#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
  if (argc != 4)
  {
    printf(1, "Usage: sbp <pid, priority_ratio, arrival_ratio, exec_cycle_ratio>\n");
    exit();
  }

  int pid = atoi(argv[1]);
  int first = atoi(argv[2]);
  int last = atoi(argv[3]);
  set_ticket(pid,first,last);
  exit();
}