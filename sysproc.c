#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_find_largest_prime_factor(void){
  int num = myproc()->tf->ebx; //register after eax
  cprintf("sys_find_largest_prime_factor()called for %d\n" ,num);
  return find_largest_prime_factor(num);
}

void sys_get_callers(int syscall_number){

}


int sys_get_parent_pid(void)
{
  struct proc *p = myproc()->parent;
  while (p->is_tracer) {
    p = p->tracer_parent;
  }
  return p->pid;
}


int
sys_print_processes(void){
  printp();
  return 0;
}

int
sys_change_queue(void) {
  int pid;
  int queue;
  argint(0, &pid);
  argint(1, &queue);
  changeq(pid, queue);
  return 0;
}

int
sys_set_bjf_process(void) {
  int pid, priority_ratio, arrival_ratio, exec_cycle_ratio;
  argint(0, &pid);
  argint(1, &priority_ratio);
  argint(2, &arrival_ratio);
  argint(3, &exec_cycle_ratio);
  set_bjf_process(pid, priority_ratio, arrival_ratio, exec_cycle_ratio);
  return 0;
}

int
sys_set_bjf(void) {
  int priority_ratio, arrival_ratio, exec_cycle_ratio;
  argint(0, &priority_ratio);
  argint(1, &arrival_ratio);
  argint(2, &exec_cycle_ratio);
  set_bjf(priority_ratio, arrival_ratio, exec_cycle_ratio);
  return 0;
}

int sys_set_ticket(void){
  int pid,first,last;
  argint(0, &pid);
  argint(1, &first);
  argint(2, &last);
  set_ticket(pid,first,last);
  return 0;
}

int sys_sem_init(void){
  int i , j;
  argint(0,&i);
  argint(1,&j);
  return sem_init(i,j);
}

int sys_sem_acquire(void){
  int i;
  argint(0,&i);
  return sem_acquire(i);
}

int sys_sem_release(void){
  int i;
  argint(0,&i);
  return sem_release(i);
}
