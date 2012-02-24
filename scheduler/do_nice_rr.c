/* The kernel call implemented in this file:
 *   m_type:	SYS_NICE
 *
 * The parameters for this kernel call are:
 *    m1_i1:	PR_ENDPT   	process number to change priority
 *    m1_i2:	PR_PRIORITY	the new priority
 */

#include "../system.h"
#include <sys/resource.h>

#if USE_NICE

/*===========================================================================*
 *				  do_nice				     *
 *===========================================================================*/
PUBLIC int do_nice(message *m_ptr)
{
/* Change process priority or stop the process. */
  /* egall */
  int proc_nr, pri, new_q, tickets;
  register struct proc *rp;

  /* Extract the message parameters and do sanity checking. */
  if(!isokendpt(m_ptr->PR_ENDPT, &proc_nr)) return EINVAL;
  if (iskerneln(proc_nr)) return(EPERM);
  pri = m_ptr->PR_PRIORITY;
  /* egall */
  if(pri > 0){
    tickets = (5 - (pri/5) );
  }
  else if(pri < 0){
    tickets = ( (-95/20)*pri + 5);
  }
  else{
    tickets = 5;
  }
  rp = proc_addr(proc_nr);

  /* The value passed in is currently between PRIO_MIN and PRIO_MAX. 
   * We have to scale this between MIN_USER_Q and MAX_USER_Q to match 
   * the kernel's scheduling queues.
   */
  if (pri < PRIO_MIN || pri > PRIO_MAX) return(EINVAL);

  new_q = MAX_USER_Q + (pri-PRIO_MIN) * (MIN_USER_Q-MAX_USER_Q+1) / 
      (PRIO_MAX-PRIO_MIN+1);
  if (new_q < MAX_USER_Q) new_q = MAX_USER_Q;	/* shouldn't happen */
  if (new_q > MIN_USER_Q) new_q = MIN_USER_Q;	/* shouldn't happen */

  /* Make sure the process is not running while changing its priority. 
   * Put the process back in its new queue if it is runnable.
   */
  RTS_LOCK_SET(rp, RTS_SYS_LOCK);
  /* egall */
  if( priv(rp)->s_flags & SYS_PROC){
    rp->p_max_priority = rp->p_priority = new_q;
  }else{
    rp->lottery_tickets = tickets;
  }
  RTS_LOCK_UNSET(rp, RTS_SYS_LOCK);

  return(OK);
}

#endif /* USE_NICE */

