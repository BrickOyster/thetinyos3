
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

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
  sys_ThreadExit(exitval);
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
  return (Tid_t) (cur_thread()->ptcb);
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  //tid 0 or self
  if(tid <= 0 || tid == sys_ThreadSelf())
    return -1;
  
  //get node with ptcb to join else null
  rlnode* temp = rlist_find(&CURPROC->ptcb_list, (PTCB*)tid, NULL);

  if(temp == NULL)
    return -1;

  //get ptcb out of node
  PTCB* threadref = temp->ptcb;

  //if detached not permited
  if(threadref->detached == 1)
    return -1;

  //thread no longer waits in queue but for joined thread
  //removing current thread from sced list since it has to wait another thread to finish, no reason to waste sched's time for it
  //rlist_remove(&cur_thread()->sched_node);
  //said to be removed 
/* _-_ */
  //update refrensce counter
  threadref->refcount++;

  //while not exited
  while(threadref->exited == 0){
    kernel_wait(&threadref->exit_cv, SCHED_USER);
    
    //if thread we joined becomes detouched
    if(threadref->detached == 1) // could the following code be in the while loop
      return -1;
  }

  //if it exits
  if(exitval!=NULL)
    *exitval = threadref->exitval;
  
  //update refrence counter
  threadref->refcount--;

  //if refcount 0 free thread -> means thread has no refrences and exited
  if(threadref->refcount == 0){
    rlist_remove(&threadref->ptcb_list_node);    // remove the ptcb from the owner process's thread list
    free(threadref);
  }

  return 0;
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
  if(!ptcb->exited)/* _-_ */
    ptcb->refcount = 1;
  else
    ptcb->refcount = 0;
  
  kernel_broadcast(&ptcb->exit_cv);
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  TCB* cur_tcb = cur_thread();
  PTCB* cur_ptcb =  cur_tcb->ptcb;
  cur_ptcb->exitval = exitval;

  cur_ptcb->exited = 1;
  cur_ptcb->refcount--;

  PCB* curproc = CURPROC;
  curproc->thread_count--;
/* _-_ */
  // if(cur_ptcb->refcount == 0){
  //  rlist_remove(&cur_ptcb->ptcb_list_node);  // remove the PTCB from the PCB's list
  //  free(cur_ptcb);                          // free the PTCB
  // }

  // wake up all the threads waiting on this one 
  kernel_broadcast(&cur_ptcb->exit_cv);

  /* if this is the last thread of the current process*/
  if(curproc->thread_count==0){
    if(get_pid(curproc)!=1){
      /* Reparent any children of the exiting process to the 
      initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
         and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);
    }
    
    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
      Do all the other cleanup we want here, close files etc. 
    */
    
    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}
