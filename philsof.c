#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){   

    int id = atoi(argv[1]);

    while (1)
    {
        if (id % 2 == 0){
            sem_acquire(id);
            sem_acquire((id+1)%5);
        } else {
            sem_acquire((id+1)%5);
            sem_acquire(id);
        }

        
        sleep(2000);
        sem_acquire(5);
        printf(1,"Philosopher %d picked up chop stick %d and %d\n",id,id,(id+1)%5);
        printf(1,"Philosopher %d is eating\n",id);
        sem_release(5);

        sem_release(id);
        sem_release(((id+1)%5));

        sleep(1000);
        sem_acquire(5);
        printf(1,"Philosopher %d put down chop stick %d and %d\n",id,id,(id+1)%5);
        printf(1,"Philosopher %d is thinking\n",id);
        sem_release(5);
        
    }
    
    exit();
}