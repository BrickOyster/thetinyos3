
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

PTCB* acquire_PTCB(TCB* tcb)
{
  PTCB* ptcb= (PTCB*) xmalloc(sizeof(PTCB));
  ptcb->tcb = tcb;

  ptcb->exited = 0;
  ptcb->detached = 0;
  ptcb->exit_cv = COND_INIT;
  ptcb->refcount = 1;//start from 1
  rlnode_init(&ptcb->ptcb_list_node, ptcb);
  rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);
  return ptcb;

}

void start_thread()
{
  int exitval;

  TCB* cur_tcb = cur_thread();

  Task call = cur_tcb->ptcb->task;
  int argl = cur_tcb->ptcb->argl;
  void* args = cur_tcb->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  TCB* curr_tcb = spawn_thread(CURPROC, start_thread);
  PTCB* new_ptcb = acquire_PTCB(curr_tcb);
  new_ptcb->task = task;
  new_ptcb->argl = argl;
  new_ptcb->args = args;
  curr_tcb->ptcb = new_ptcb;

  CURPROC->thread_count++;

  wakeup(curr_tcb);
	
  return (Tid_t) new_ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread();
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{ 
  PTCB* ptcb = (PTCB*) tid;
  
  if(ptcb == NULL)
    return -1;
  
  if(rlist_find(&CURPROC->ptcb_list,ptcb,NULL) == NULL)
    return -1;
  
  ptcb->detached = 1;
  ptcb->refcount = 0;
  
  kernel_broadcast(&ptcb->exit_cv);
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

