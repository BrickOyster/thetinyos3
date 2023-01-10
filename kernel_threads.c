#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

PTCB* acquire_PTCB(TCB* tcb)
{
  /* allocating the needed space to create a new process thread control block*/
  PTCB* ptcb= (PTCB*) xmalloc(sizeof(PTCB));
  /* making the needed connection with tcb and initializing
   * the rest of its fields */
  ptcb->tcb = tcb;
  ptcb->exited = 0;
  ptcb->detached = 0;
  ptcb->exit_cv = COND_INIT;
  ptcb->refcount = 0;

  /* at last we create an rlnode for the new ptcb and adding it 
   * to the current process' ptcb list*/
  rlnode_init(&ptcb->ptcb_list_node, ptcb);
  rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);
  return ptcb;

}

void start_thread()
{
  int exitval;

  /* getting current thread */
  TCB* cur_tcb = cur_thread();

  /* passing the function pointer to call for our new
   * thread and getting the args and argl for it */
  Task call = cur_tcb->ptcb->task;
  int argl = cur_tcb->ptcb->argl;
  void* args = cur_tcb->ptcb->args;

  /* start executing the wanted function and passing the exit value
   * to exitval, at last since the thread has finished its task we call thread exit */
  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  TCB* curr_tcb = spawn_thread(CURPROC, start_thread);

  /* we initialize the fields of the new ptcb, define its task & arguments
   * and connecting it with the thread's ptcb */
  PTCB* new_ptcb = acquire_PTCB(curr_tcb);
  new_ptcb->task = task;
  new_ptcb->argl = argl;
  new_ptcb->args = args;
  curr_tcb->ptcb = new_ptcb;

  /* increasing current process thread counter by one */
  CURPROC->thread_count++;

  /* the new thread is now ready to run so it has to wakeup */
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
  /* if tid is illegal or corresponds to itself return -1*/
  if(tid <= 0 || tid == sys_ThreadSelf())
    return -1;
  
  //get node with ptcb to join else null
  /* searching for the ptcb of the thread we want to join, if it
   * does not exist it returns NULL*/
  rlnode* temp = rlist_find(&CURPROC->ptcb_list, (PTCB*)tid, NULL);

  /* tid does not correspond to a thread of the current process*/
  if(temp == NULL)
    return -1;

  /* passing ptcb out of the node */
  PTCB* threadref = temp->ptcb;

  /* if the thread we want to join is detached then join is
   * not permitted and returns -1*/
  if(threadref->detached == 1)
    return -1;

  /* update reference counter */
  threadref->refcount++;

  /* while the thread we joined has not finished then sleep*/
  while(threadref->exited == 0 && threadref->detached == 0)
    kernel_wait(&threadref->exit_cv, SCHED_USER);
  
  /* update reference counter */
  threadref->refcount--;

  /* if the thread we joined becomes detached */
  if(threadref->detached == 1)
    return -1;

  /* at exit */
  if(exitval!=NULL)
    *exitval = threadref->exitval;
  

  /* if refcount is 0 then free the thread since 
   * the thread has no more refrences and  has exited */
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

  /* checking to see if the given ptcb exists in the current process
   * at failure it returns NULL and then we return -1*/
  if(rlist_find(&CURPROC->ptcb_list,ptcb,NULL) == NULL)
    return -1;
  
  if(ptcb->exited)
    return -1;

  /* marking the ptcb as detached */
  ptcb->detached = 1;

  /* singals all the waiters */
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
  /* defining ptcb's exit value */
  cur_ptcb->exitval = exitval;

  /* marking ptcb as exited and updating reference counter */
  cur_ptcb->exited = 1;

  /* decreasing current process' threads by 1 */
  PCB* curproc = CURPROC;
  curproc->thread_count--;

  /* wake up all the threads waiting on this one */ 
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

    /* checking if list has emptied and freeing any remaining ptcbs */
    while(!is_rlist_empty(&curproc->ptcb_list))
     free(rlist_pop_front(&curproc->ptcb_list)->ptcb); 

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}
