#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "stride.h"

struct {
  ull stride[NPROC + 1];   //Store the current stride of each process. (x)
  ull st[4 * (NPROC + 1)]; //Segment tree (priority queue)
  int idstack[NPROC], tp;  //Stack of indexes not used in the BIT by any process
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

/*-----------------SEGMENT TREE-------------------*/

void
build(int p, int l, int r)
{
  int m = (l + r) / 2, pidl, pidr;
  if (l == r) { ptable.st[p] = l; return; }
  build(left(p), l, m); build(right(p), m + 1, r);
  pidl = ptable.st[left(p)]; pidr = ptable.st[right(p)];
  ptable.st[p] = ptable.stride[pidl] <= ptable.stride[pidr] ? pidl : pidr;
}

int
query(int p, int l, int r, int i, int j)
{
  int m = (l + r) / 2, pidl, pidr;
  if (i > r || j < l) return 0;
  if (i <= l && j >= r) return ptable.st[p];
  pidl = query(left(p), l, m, i, j);
  pidr = query(right(p), m + 1, r, i, j);
  return ptable.stride[pidl] <= ptable.stride[pidr] ? pidl : pidr;
}

void
update(int p, int l, int r, int i)
{
  int m = (l + r) / 2, pidl, pidr;
  if (i > r || i < l || (i == l && i == r)) return;
  update(left(p), l, m, i); update(right(p), m + 1, r, i);
  pidl = ptable.st[left(p)]; pidr = ptable.st[right(p)];
  ptable.st[p] = ptable.stride[pidl] <= ptable.stride[pidr] ? pidl : pidr;
}

void
clean()
{
  acquire(&ptable.lock);
  int i;
  for (ptable.tp = 0; ptable.tp < NPROC; ptable.tp++)
    ptable.idstack[ptable.tp] = NPROC - ptable.tp;
  for (i = 0; i <= NPROC; i++) ptable.stride[i] = INF;
  build(1, 0, NPROC);
  release(&ptable.lock);
}

int find(){
  int i, menor = NPROC;
  for(i = 0; i <= NPROC; i++)
    if(ptable.stride[i] < ptable.stride[menor]) menor = i;
  return menor;
}

void printdeb() {
  int i;
  cprintf("\n");
  for (i = 0; i <= NPROC; i++) cprintf("[%d]", ptable.stride[i] == INF ? -1 : ptable.stride[i]);
  cprintf("\n");
}

/*---------------------------------------*/


void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  clean();
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  int i;
  char *sp;
  struct proc *p;

  acquire(&ptable.lock);
  if (ptable.tp > 0) {
    i = ptable.idstack[--ptable.tp];
    p = &ptable.proc[i - 1]; //Gets a free position
    goto found;
  }
  release(&ptable.lock);
  return 0;

found:
  p->pid = i;
  p->state = EMBRYO;
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
  p->tickets = DEFT;
  p->prevstride = 0;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  acquire(&ptable.lock);
  ptable.stride[p->pid] = 0;
  update(1, 0, NPROC, p->pid);
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(int ntick)
{
  int i, pid;
  struct proc *np;

  //If the number of tickets is less than the minimum allowed, set the default amount
  if(ntick < MINT) ntick = DEFT;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;
  np->tickets = min(ntick, MAXT);
  np->prevstride = 0;
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));

  pid = np->pid;

  // lock to force the compiler to emit the np->state write last.
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  ptable.stride[pid] = 0;
  update(1, 0, NPROC, pid);
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  // cprintf("%d\n", proc->tickets); -> Used for tests
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
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

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->parent = 0; p->name[0] = 0;
        p->killed = 0;
        ptable.idstack[ptable.tp++] = p->pid;
        ptable.stride[p->pid] = INF;
        update(1, 0, NPROC, p->pid);
        p->pid = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
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
  struct proc *p;
  int pid;

  for(;;){
    // Enable interrupts on this processor.
    //cprintf("Init\n");
    sti();
    //cprintf("Foi?\n");
    // int cont = 0;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // printdeb();
    //if ((pid = find()) != 0) {
    if ((pid = query(1, 0, NPROC, 0, NPROC)) != 0) {
      // cprintf("%d\n\n", pid);
      p = &ptable.proc[pid - 1];
      if (p->state == RUNNABLE) {
        // cprintf(">process: %d, numtick: %d\n", p->pid, p->tickets);

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        p->prevstride = ptable.stride[pid]; ptable.stride[pid] = INF;
        update(1, 0, NPROC, pid);
        proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&cpu->scheduler, proc->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        proc = 0;
      }
    }
    // printdeb();
    release(&ptable.lock);
    // cprintf("Oi cont++!\n");
    // cont++;
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  ptable.stride[proc->pid] = (proc->prevstride + CONS / proc->tickets) % INF;
  update(1, 0, NPROC, proc->pid);
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
  if(proc == 0)
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
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

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
    if (p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      ptable.stride[p->pid] = (p->prevstride + CONS / p->tickets) % INF;
      update(1, 0, NPROC, p->pid);
    }
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
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        ptable.stride[pid] = 0;
        update(1, 0, NPROC, pid);
      }
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
