#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->creation_time = ticks;
  p->waiting_in_queue_cycle = 0;
  if (p->pid == 1 || p->pid == 2)
    p->queue_lvl = ROUND_ROBIN_LVL;
  else
    p->queue_lvl = LOT_LVL;
  p->exec_cycle = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

void changeq(int pid, int queue)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
      p->queue_lvl = queue;
  }
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
void printp(void)
{
  cprintf("name   pid   status   queue   init time   effectiveness   rank   cpu cycles   tickets \n");
  struct proc *p;
  int t;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == 0)
      continue;
    t=p->last_tick - p->first_tick ;
    cprintf("%s  ", p->name);
    cprintf("%d  ", p->pid);
    if (p->state == SLEEPING)
      cprintf("SLEEPING  ");
    else if (p->state == RUNNABLE)
      cprintf("RUNNABLE  ");
    else if (p->state == UNUSED)
      cprintf("UNUSED  ");
    else if (p->state == EMBRYO)
      cprintf("EMBRYO  ");
    else if (p->state == RUNNING)
      cprintf("RUNNING  ");
    else if (p->state == ZOMBIE)
      cprintf("ZOMBIE  ");
    cprintf("%d  ", p->queue_lvl);
    cprintf("%d  ", p->creation_time);
    cprintf("%d/%d/%d  ", p->arrival_ratio, p->priority_ratio, p->exec_cycle_ratio);
    cprintf("%d  ", (int)get_rank(p));
    cprintf("%d  \n", p->exec_cycle);
    cprintf("%d  \n", t);
  }
  release(&ptable.lock);
}
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    p = round_robin_sched();

    if (p == 0)
      p = lot_sched();

    if (p == 0)
      p = bjf_sched();

    if (p == 0)
    {
      release(&ptable.lock);
      continue;
    }
    age();
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    p->waiting_in_queue_cycle = 0;
    struct proc *pc;
    for (pc = ptable.proc; pc < &ptable.proc[NPROC]; pc++)
    {
      if (pc->pid == p->pid)
        continue;
      pc->waiting_in_queue_cycle++;
    }
    swtch(&(c->scheduler), p->context);
    switchkvm();
    c->proc = 0;
    release(&ptable.lock);
  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->exec_cycle += 1;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

double sq(double s){
    double root=s/3;
    int i;
    if(s<=0)return 0;
    for(i=0;i<32;i++){
        root=(root+s / root)/2;}
        return root;
}
int find_largest_prime_factor(int n) {
   int i, max = -1;
   while(n % 2 == 0) {
      max = 2;
      n = n/2; 
   }
   for(i = 3; i <= sq(n); i=i+2){ 
   
      while(n % i == 0) {
         max = i;
         n = n/i;
      }
   }
   if(n > 2) {
      max = n;
   }
   return max;
}


void age(void)
{
  struct proc *p = 0;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE)
      continue;

    if (p->waiting_in_queue_cycle > 8000)
    {

      if (p->queue_lvl == LOT_LVL)
      {
        p->queue_lvl = ROUND_ROBIN_LVL;
      }
      if (p->queue_lvl == BJF_LVL)
      {
        p->queue_lvl = LOT_LVL;
      }
      p->waiting_in_queue_cycle = 0;
    }
  }
}

float get_rank(struct proc *p)
{
  return (float)(p->priority * p->priority_ratio + p->arrival * p->arrival_ratio + p->exec_cycle * p->exec_cycle_ratio) / 10;
}

unsigned short lfsr = 0xACE1u;
  unsigned bit;

unsigned rand()
{
  bit  = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5) ) & 1;
  return lfsr =  (lfsr >> 1) | (bit << 15);
}


struct proc *
round_robin_sched(void)
{
  struct proc *chosen_proc = 0;

  int now = ticks;
  int max_process_time = -1e5;

  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE || p->queue_lvl != ROUND_ROBIN_LVL)
    {
      continue;
    }

    if (now - p->last_cpu_time > max_process_time)
    {
      max_process_time = now - p->last_cpu_time;
      chosen_proc = p;
      p->last_cpu_time = now;
    }
  }
  return chosen_proc;
}

struct proc *
bjf_sched(void)
{
  struct proc *chosen_proc = 0;
  int min_rank = -1, rank = -1;

  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE || p->queue_lvl != BJF_LVL)
      continue;

    rank = get_rank(p);
    if (rank < min_rank || min_rank == -1)
    {
      chosen_proc = p;
      min_rank = rank;
    }
  }

  return chosen_proc;
}

struct proc *
lot_sched(void)
{
  struct proc *chosen_proc = 0;
  int ticket=rand()%200;
  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE || p->queue_lvl != LOT_LVL)
      continue;

    if (ticket >= p->first_tick  &&  ticket <= p->last_tick)
    {
      chosen_proc = p;
    }
  }
  return chosen_proc;
}

int set_bjf_process(int pid, int priority_ratio, int arrival_ratio, int exec_cycle_ratio)
{
  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->arrival_ratio = arrival_ratio;
      p->priority_ratio = priority_ratio;
      p->exec_cycle_ratio = exec_cycle_ratio;
    }
  }
  return pid;
}

int set_bjf(int priority_ratio, int arrival_ratio, int exec_cycle_ratio)
{
  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    p->arrival_ratio = arrival_ratio;
    p->priority_ratio = priority_ratio;
    p->exec_cycle_ratio = exec_cycle_ratio;
  }
  return 0;
}

int set_ticket(int pid,int first,int last){
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->first_tick=first;
      p->last_tick=last;
    }
  }
  return pid;
}

struct semaphore {
  int value;
  int locked;
  int owner;
  struct spinlock lock;
};

struct semaphore chop_stick[6];

int sem_init(int i, int v){

  acquire(&chop_stick[i].lock);

  if (chop_stick[i].locked == 0) {
    chop_stick[i].locked = 1;
    chop_stick[i].value = v;
    chop_stick[i].owner = -1;
  } else {
    release(&chop_stick[i].lock);
    return -1;
  }  

  release(&chop_stick[i].lock);

  return 0;

}

int sem_acquire(int i){

  acquire(&chop_stick[i].lock);

  if (chop_stick[i].value >= 1) {
     chop_stick[i].value = chop_stick[i].value - 1;
     chop_stick[i].owner = i;
  } else {
    while (chop_stick[i].value < 1) sleep(&chop_stick[i],&chop_stick[i].lock);
    chop_stick[i].value = chop_stick[i].value - 1;
    chop_stick[i].owner = i;
  }

  release(&chop_stick[i].lock);

  return 0;
}

int sem_release(int i){
  
  acquire(&chop_stick[i].lock);
  chop_stick[i].value = chop_stick[i].value + 1;
  chop_stick[i].owner = -1;
  wakeup(&chop_stick[i]); 
  release(&chop_stick[i].lock);

  return 0;
}
