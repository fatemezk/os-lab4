#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
    int queue = atoi(argv[2]);
    int pid = atoi(argv[1]);
    change_queue(pid, queue);
    exit();
}