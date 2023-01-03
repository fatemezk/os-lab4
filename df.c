
#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {

    for (int i = 0 ; i < 6 ; i++) sem_init(i,1);

    for (int i = 0 ; i < 5 ; i++){
        int pid = fork();
        if (pid == 0){
            char num = i + 48;
            char arg[1];
            arg[0] = num;
            char *map_handler[] = {(char *) "philsof", arg, 0};
            exec("philsof", map_handler);
        }
    }
    for (int i = 0 ; i < 5 ; i++) wait();
    exit();
}