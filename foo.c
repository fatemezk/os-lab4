#include "types.h"
#include "stat.h"
#include "user.h"

#define NUMPROCS 10
#define MAXNUM 99999999999

int
main(int argc, char *argv[])
{
    int pid;

    for(int i=0; i < NUMPROCS; i++)
    {
        if((pid = fork()) < 0)
        {
            printf(0,"error occured!\n");
            exit();
        }
        if(pid == 0)
            break;
    }

    int k = 1;

    if(pid == 0)
    {
        for (int i = 0; i < MAXNUM; i++)
        {
            for (int j = 0; j < MAXNUM; j++)
            {
                for (int t = 0; t < MAXNUM; t++)
                {
                    k ++;
                }
            }
        }
        exit();
    }

    for (int i = 0; i < NUMPROCS; i++)
        wait();
    exit();
}