/* This file contains the main program of the process manager and some related
 * procedures.  When MINIX starts up, the kernel runs for a little while,
 * initializing itself and its tasks, and then it runs PM and FS.  Both PM
 * and FS initialize themselves as far as they can. PM asks the kernel for
 * all free memory and starts serving requests.
 *
 * The entry points into this file are:
 *   main:	starts PM running
 *   setreply:	set the reply to be sent to process making an PM system call
 */

#include "pm.h"
#include <minix/keymap.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/endpoint.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <string.h>
#include <archconst.h>
#include <archtypes.h>
#include <env.h>
#include "mproc.h"
#include "param.h"

#include "../../kernel/const.h"
#include "../../kernel/config.h"
#include "../../kernel/proc.h"

#if ENABLE_SYSCALL_STATS
EXTERN unsigned long calls_stats[NCALLS];
#endif

FORWARD _PROTOTYPE( void get_work, (void)				);
FORWARD _PROTOTYPE( void pm_init, (void)				);
FORWARD _PROTOTYPE( int get_nice_value, (int queue)			);
FORWARD _PROTOTYPE( void get_mem_chunks, (struct memory *mem_chunks) 	);
FORWARD _PROTOTYPE( void patch_mem_chunks, (struct memory *mem_chunks, 
	struct mem_map *map_ptr) 	);
FORWARD _PROTOTYPE( void do_x86_vm, (struct memory mem_chunks[NR_MEMS])	);
FORWARD _PROTOTYPE( void send_work, (void)				);
FORWARD _PROTOTYPE( void handle_fs_reply, (message *m_ptr)		);

#define click_to_round_k(n) \
	((unsigned) ((((unsigned long) (n) << CLICK_SHIFT) + 512) / 1024))

/*egall*/
EXTERN int algo_type;

/*===========================================================================*
 *				main					     *
 *===========================================================================*/
PUBLIC int main()
{
/* Main routine of the process manager. */
  int result, s, proc_nr;
  struct mproc *rmp;
  sigset_t sigset;

  pm_init();			/* initialize process manager tables */

  /* This is PM's main loop-  get work and do it, forever and forever. */
  while (TRUE) {
	get_work();		/* wait for an PM system call */

	/* Check for system notifications first. Special cases. */
	switch(call_nr)
	{
	case SYN_ALARM:
		pm_expire_timers(m_in.NOTIFY_TIMESTAMP);
		result = SUSPEND;		/* don't reply */
		break;
	case SYS_SIG:				/* signals pending */
		sigset = m_in.NOTIFY_ARG;
		if (sigismember(&sigset, SIGKSIG))  {
			(void) ksig_pending();
		} 
		result = SUSPEND;		/* don't reply */
		break;
	case PM_GET_WORK:
		if (who_e == FS_PROC_NR)
		{
			send_work();
			result= SUSPEND;		/* don't reply */
		}
		else
			result= ENOSYS;
		break;
	case PM_EXIT_REPLY:
	case PM_REBOOT_REPLY:
	case PM_EXEC_REPLY:
	case PM_CORE_REPLY:
	case PM_EXIT_REPLY_TR:
		if (who_e == FS_PROC_NR)
		{
			handle_fs_reply(&m_in);
			result= SUSPEND;		/* don't reply */
		}
		else
			result= ENOSYS;
		break;
	case ALLOCMEM:
		result= do_allocmem();
		break;
	case FORK_NB:
		result= do_fork_nb();
		break;
	case EXEC_NEWMEM:
		result= exec_newmem();
		break;
	case EXEC_RESTART:
		result= do_execrestart();
		break;
	case PROCSTAT:
		result= do_procstat();
		break;
	case GETPROCNR:
		result= do_getprocnr();
		break;
	default:
		/* Else, if the system call number is valid, perform the
		 * call.
		 */
		if ((unsigned) call_nr >= NCALLS) {
			result = ENOSYS;
		} else {
#if ENABLE_SYSCALL_STATS
			calls_stats[call_nr]++;
#endif
			result = (*call_vec[call_nr])();
		}
		break;
	}

	/* Send the results back to the user to indicate completion. */
	if (result != SUSPEND) setreply(who_p, result);

	swap_in();		/* maybe a process can be swapped in? */

	/* Send out all pending reply messages, including the answer to
	 * the call just made above.  The processes must not be swapped out.
	 */
	for (proc_nr=0, rmp=mproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		/* In the meantime, the process may have been killed by a
		 * signal (e.g. if a lethal pending signal was unblocked)
		 * without the PM realizing it. If the slot is no longer in
		 * use or just a zombie, don't try to reply.
		 */
		if ((rmp->mp_flags & (REPLY | ONSWAP | IN_USE | ZOMBIE)) ==
		   (REPLY | IN_USE)) {
			if ((s=send(rmp->mp_endpoint, &rmp->mp_reply)) != OK) {
				printf("PM can't reply to %d (%s)\n",
					rmp->mp_endpoint, rmp->mp_name);
				panic(__FILE__, "PM can't reply", NO_NUM);
			}
			rmp->mp_flags &= ~REPLY;
		}
	}
  }
  return(OK);
}

/*===========================================================================*
 *				get_work				     *
 *===========================================================================*/
PRIVATE void get_work()
{
/* Wait for the next message and extract useful information from it. */
  if (receive(ANY, &m_in) != OK)
	panic(__FILE__,"PM receive error", NO_NUM);
  who_e = m_in.m_source;	/* who sent the message */
  if(pm_isokendpt(who_e, &who_p) != OK)
	panic(__FILE__, "PM got message from invalid endpoint", who_e);
  call_nr = m_in.m_type;	/* system call number */

  /* Process slot of caller. Misuse PM's own process slot if the kernel is
   * calling. This can happen in case of synchronous alarms (CLOCK) or or 
   * event like pending kernel signals (SYSTEM).
   */
  mp = &mproc[who_p < 0 ? PM_PROC_NR : who_p];
  if(who_p >= 0 && mp->mp_endpoint != who_e) {
	panic(__FILE__, "PM endpoint number out of sync with source",
		mp->mp_endpoint);
  }
}

/*===========================================================================*
 *				setreply				     *
 *===========================================================================*/
PUBLIC void setreply(proc_nr, result)
int proc_nr;			/* process to reply to */
int result;			/* result of call (usually OK or error #) */
{
/* Fill in a reply message to be sent later to a user process.  System calls
 * may occasionally fill in other fields, this is only for the main return
 * value, and for setting the "must send reply" flag.
 */
  register struct mproc *rmp = &mproc[proc_nr];

  if(proc_nr < 0 || proc_nr >= NR_PROCS)
      panic(__FILE__,"setreply arg out of range", proc_nr);

  rmp->mp_reply.reply_res = result;
  rmp->mp_flags |= REPLY;	/* reply pending */

  if (rmp->mp_flags & ONSWAP)
	swap_inqueue(rmp);	/* must swap this process back in */
}

/*===========================================================================*
 *				pm_init					     *
 *===========================================================================*/
PRIVATE void pm_init()
{
/* Initialize the process manager. 
 * Memory use info is collected from the boot monitor, the kernel, and
 * all processes compiled into the system image. Initially this information
 * is put into an array mem_chunks. Elements of mem_chunks are struct memory,
 * and hold base, size pairs in units of clicks. This array is small, there
 * should be no more than 8 chunks. After the array of chunks has been built
 * the contents are used to initialize the hole list. Space for the hole list
 * is reserved as an array with twice as many elements as the maximum number
 * of processes allowed. It is managed as a linked list, and elements of the
 * array are struct hole, which, in addition to storage for a base and size in 
 * click units also contain space for a link, a pointer to another element.
*/
  int s;
  static struct boot_image image[NR_BOOT_PROCS];
  register struct boot_image *ip;
  static char core_sigs[] = { SIGQUIT, SIGILL, SIGTRAP, SIGABRT,
			SIGEMT, SIGFPE, SIGUSR1, SIGSEGV, SIGUSR2 };
  static char ign_sigs[] = { SIGCHLD, SIGWINCH, SIGCONT };
  static char mess_sigs[] = { SIGTERM, SIGHUP, SIGABRT, SIGQUIT };
  register struct mproc *rmp;
  register char *sig_ptr;
  phys_clicks total_clicks, minix_clicks, free_clicks;
  message mess;
  struct mem_map mem_map[NR_LOCAL_SEGS];
  struct memory mem_chunks[NR_MEMS];

  /* Initialize process table, including timers. */
  for (rmp=&mproc[0]; rmp<&mproc[NR_PROCS]; rmp++) {
	tmr_inittimer(&rmp->mp_timer);

	rmp->mp_fs_call= PM_IDLE;
	rmp->mp_fs_call2= PM_IDLE;
  }

  /* Build the set of signals which cause core dumps, and the set of signals
   * that are by default ignored.
   */
  sigemptyset(&core_sset);
  for (sig_ptr = core_sigs; sig_ptr < core_sigs+sizeof(core_sigs); sig_ptr++)
	sigaddset(&core_sset, *sig_ptr);
  sigemptyset(&ign_sset);
  for (sig_ptr = ign_sigs; sig_ptr < ign_sigs+sizeof(ign_sigs); sig_ptr++)
	sigaddset(&ign_sset, *sig_ptr);

  /* Obtain a copy of the boot monitor parameters and the kernel info struct.  
   * Parse the list of free memory chunks. This list is what the boot monitor 
   * reported, but it must be corrected for the kernel and system processes.
   */
  if ((s=sys_getmonparams(monitor_params, sizeof(monitor_params))) != OK)
      panic(__FILE__,"get monitor params failed",s);
  get_mem_chunks(mem_chunks);
  if ((s=sys_getkinfo(&kinfo)) != OK)
      panic(__FILE__,"get kernel info failed",s);

  /* Get the memory map of the kernel to see how much memory it uses. */
  if ((s=get_mem_map(SYSTASK, mem_map)) != OK)
  	panic(__FILE__,"couldn't get memory map of SYSTASK",s);
  minix_clicks = (mem_map[S].mem_phys+mem_map[S].mem_len)-mem_map[T].mem_phys;
  patch_mem_chunks(mem_chunks, mem_map);

  /* Initialize PM's process table. Request a copy of the system image table 
   * that is defined at the kernel level to see which slots to fill in.
   */
  if (OK != (s=sys_getimage(image))) 
  	panic(__FILE__,"couldn't get image table: %d\n", s);
  procs_in_use = 0;				/* start populating table */
  for (ip = &image[0]; ip < &image[NR_BOOT_PROCS]; ip++) {		
  	if (ip->proc_nr >= 0) {			/* task have negative nrs */
  		procs_in_use += 1;		/* found user process */

		/* Set process details found in the image table. */
		rmp = &mproc[ip->proc_nr];	
  		strncpy(rmp->mp_name, ip->proc_name, PROC_NAME_LEN); 
		rmp->mp_parent = RS_PROC_NR;
		rmp->mp_nice = get_nice_value(ip->priority);
  		sigemptyset(&rmp->mp_sig2mess);
  		sigemptyset(&rmp->mp_ignore);	
  		sigemptyset(&rmp->mp_sigmask);
  		sigemptyset(&rmp->mp_catch);
		if (ip->proc_nr == INIT_PROC_NR) {	/* user process */
  			rmp->mp_procgrp = rmp->mp_pid = INIT_PID;
			rmp->mp_flags |= IN_USE; 
		}
		else {					/* system process */
  			rmp->mp_pid = get_free_pid();
			rmp->mp_flags |= IN_USE | DONT_SWAP | PRIV_PROC; 
  			for (sig_ptr = mess_sigs; 
				sig_ptr < mess_sigs+sizeof(mess_sigs); 
				sig_ptr++)
			sigaddset(&rmp->mp_sig2mess, *sig_ptr);
		}

		/* Get kernel endpoint identifier. */
		rmp->mp_endpoint = ip->endpoint;

  		/* Get memory map for this process from the kernel. */
		if ((s=get_mem_map(ip->proc_nr, rmp->mp_seg)) != OK)
  			panic(__FILE__,"couldn't get process entry",s);
		if (rmp->mp_seg[T].mem_len != 0) rmp->mp_flags |= SEPARATE;
		minix_clicks += rmp->mp_seg[S].mem_phys + 
			rmp->mp_seg[S].mem_len - rmp->mp_seg[T].mem_phys;
  		patch_mem_chunks(mem_chunks, rmp->mp_seg);

		/* Tell FS about this system process. */
		mess.PR_SLOT = ip->proc_nr;
		mess.PR_PID = rmp->mp_pid;
		mess.PR_ENDPT = rmp->mp_endpoint;
  		if (OK != (s=send(FS_PROC_NR, &mess)))
			panic(__FILE__,"can't sync up with FS", s);
  	}
  }

  /* Override some details. INIT, PM, FS and RS are somewhat special. */
  mproc[PM_PROC_NR].mp_pid = PM_PID;		/* PM has magic pid */
  mproc[RS_PROC_NR].mp_parent = INIT_PROC_NR;	/* INIT is root */
  sigfillset(&mproc[PM_PROC_NR].mp_ignore); 	/* guard against signals */

  /* Tell FS that no more system processes follow and synchronize. */
  mess.PR_ENDPT = NONE;
  if (sendrec(FS_PROC_NR, &mess) != OK || mess.m_type != OK)
	panic(__FILE__,"can't sync up with FS", NO_NUM);

#if ENABLE_BOOTDEV
  /* Possibly we must correct the memory chunks for the boot device. */
  if (kinfo.bootdev_size > 0) {
      mem_map[T].mem_phys = kinfo.bootdev_base >> CLICK_SHIFT;
      mem_map[T].mem_len = 0;
      mem_map[D].mem_len = (kinfo.bootdev_size+CLICK_SIZE-1) >> CLICK_SHIFT;
      patch_mem_chunks(mem_chunks, mem_map);
  }
#endif /* ENABLE_BOOTDEV */

  /* Withhold some memory from x86 VM */
  do_x86_vm(mem_chunks);

  /* Initialize tables to all physical memory and print memory information. */
  printf("Physical memory:");
  mem_init(mem_chunks, &free_clicks);
  total_clicks = minix_clicks + free_clicks;
  printf(" total %u KB,", click_to_round_k(total_clicks));
  printf(" system %u KB,", click_to_round_k(minix_clicks));
  printf(" free %u KB.\n", click_to_round_k(free_clicks));
#if (CHIP == INTEL)
  uts_val.machine[0] = 'i';
  strcpy(uts_val.machine + 1, itoa(getprocessor()));
#endif
}

/*===========================================================================*
 *				get_nice_value				     *
 *===========================================================================*/
PRIVATE int get_nice_value(queue)
int queue;				/* store mem chunks here */
{
/* Processes in the boot image have a priority assigned. The PM doesn't know
 * about priorities, but uses 'nice' values instead. The priority is between 
 * MIN_USER_Q and MAX_USER_Q. We have to scale between PRIO_MIN and PRIO_MAX.
 */ 
  int nice_val = (queue - USER_Q) * (PRIO_MAX-PRIO_MIN+1) / 
      (MIN_USER_Q-MAX_USER_Q+1);
  if (nice_val > PRIO_MAX) nice_val = PRIO_MAX;	/* shouldn't happen */
  if (nice_val < PRIO_MIN) nice_val = PRIO_MIN;	/* shouldn't happen */
  return nice_val;
}

/*===========================================================================*
 *				get_mem_chunks				     *
 *===========================================================================*/
PRIVATE void get_mem_chunks(mem_chunks)
struct memory *mem_chunks;			/* store mem chunks here */
{
/* Initialize the free memory list from the 'memory' boot variable.  Translate
 * the byte offsets and sizes in this list to clicks, properly truncated.
 */
  long base, size, limit;
  int i;
  struct memory *memp;
  
  /* Obtain and parse memory from system environment. */
  if(env_memory_parse(mem_chunks, NR_MEMS) != OK)
	panic(__FILE__,"couldn't obtain memory chunks", NO_NUM);

  /* Round physical memory to clicks. */
  for (i = 0; i < NR_MEMS; i++) {
	memp = &mem_chunks[i];		/* next mem chunk is stored here */
	base = mem_chunks[i].base;
	size = mem_chunks[i].size;
	limit = base + size;	
	base = (base + CLICK_SIZE-1) & ~(long)(CLICK_SIZE-1);
	limit &= ~(long)(CLICK_SIZE-1);
	if (limit <= base) {
		memp->base = memp->size = 0;
	} else {
		memp->base = base >> CLICK_SHIFT;
		memp->size = (limit - base) >> CLICK_SHIFT;
	}
  }
}

/*===========================================================================*
 *				patch_mem_chunks			     *
 *===========================================================================*/
PRIVATE void patch_mem_chunks(mem_chunks, map_ptr)
struct memory *mem_chunks;			/* store mem chunks here */
struct mem_map *map_ptr;			/* memory to remove */
{
/* Remove server memory from the free memory list. The boot monitor
 * promises to put processes at the start of memory chunks. The 
 * tasks all use same base address, so only the first task changes
 * the memory lists. The servers and init have their own memory
 * spaces and their memory will be removed from the list. 
 */
  struct memory *memp;
  for (memp = mem_chunks; memp < &mem_chunks[NR_MEMS]; memp++) {
	if (memp->base == map_ptr[T].mem_phys) {
		memp->base += map_ptr[T].mem_len + map_ptr[S].mem_vir;
		memp->size -= map_ptr[T].mem_len + map_ptr[S].mem_vir;
		break;
	}
  }
  if (memp >= &mem_chunks[NR_MEMS])
  {
	panic(__FILE__,"patch_mem_chunks: can't find map in mem_chunks, start",
		map_ptr[T].mem_phys);
  }
}

#define PAGE_SIZE	4096
#define PAGE_TABLE_COVER (1024*PAGE_SIZE)
/*=========================================================================*
 *				do_x86_vm				   *
 *=========================================================================*/
PRIVATE void do_x86_vm(mem_chunks)
struct memory mem_chunks[NR_MEMS];
{
	phys_bytes high, bytes;
	phys_clicks clicks, base_click;
	unsigned pages;
	int i, r;

	/* Compute the highest memory location */
	high= 0;
	for (i= 0; i<NR_MEMS; i++)
	{
		if (mem_chunks[i].size == 0)
			continue;
		if (mem_chunks[i].base + mem_chunks[i].size > high)
			high= mem_chunks[i].base + mem_chunks[i].size;
	}

	high <<= CLICK_SHIFT;
#if VERBOSE_VM
	printf("do_x86_vm: found high 0x%x\n", high);
#endif

	/* The number of pages we need is one for the page directory, enough
	 * page tables to cover the memory, and one page for alignement.
	 */
	pages= 1 + (high + PAGE_TABLE_COVER-1)/PAGE_TABLE_COVER + 1;
	bytes= pages*PAGE_SIZE;
	clicks= (bytes + CLICK_SIZE-1) >> CLICK_SHIFT;

#if VERBOSE_VM
	printf("do_x86_vm: need %d pages\n", pages);
	printf("do_x86_vm: need %d bytes\n", bytes);
	printf("do_x86_vm: need %d clicks\n", clicks);
#endif

	for (i= 0; i<NR_MEMS; i++)
	{
		if (mem_chunks[i].size <= clicks)
			continue;
		break;
	}
	if (i >= NR_MEMS)
		panic("PM", "not enough memory for VM page tables?", NO_NUM);
	base_click= mem_chunks[i].base;
	mem_chunks[i].base += clicks;
	mem_chunks[i].size -= clicks;

#if VERBOSE_VM
	printf("do_x86_vm: using 0x%x clicks @ 0x%x\n", clicks, base_click);
#endif
	r= sys_vm_setbuf(base_click << CLICK_SHIFT, clicks << CLICK_SHIFT,
		high);
	if (r != 0)
		printf("do_x86_vm: sys_vm_setbuf failed: %d\n", r);
}

/*=========================================================================*
 *				send_work				   *
 *=========================================================================*/
PRIVATE void send_work()
{
	int r, call;
	struct mproc *rmp;
	message m;

	m.m_type= PM_IDLE;
	for (rmp= mproc; rmp < &mproc[NR_PROCS]; rmp++)
	{
		call= rmp->mp_fs_call;
		if (call == PM_IDLE)
			call= rmp->mp_fs_call2;
		if (call == PM_IDLE)
			continue;
		switch(call)
		{
		case PM_STIME:
			m.m_type= call;
			m.PM_STIME_TIME= boottime;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			/* Wakeup the original caller */
			setreply(rmp-mproc, OK);
			break;

		case PM_SETSID:
			m.m_type= call;
			m.PM_SETSID_PROC= rmp->mp_endpoint;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			/* Wakeup the original caller */
			setreply(rmp-mproc, rmp->mp_procgrp);
			break;

		case PM_SETGID:
			m.m_type= call;
			m.PM_SETGID_PROC= rmp->mp_endpoint;
			m.PM_SETGID_EGID= rmp->mp_effgid;
			m.PM_SETGID_RGID= rmp->mp_realgid;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			/* Wakeup the original caller */
			setreply(rmp-mproc, OK);
			break;

		case PM_SETUID:
			m.m_type= call;
			m.PM_SETUID_PROC= rmp->mp_endpoint;
			m.PM_SETUID_EGID= rmp->mp_effuid;
			m.PM_SETUID_RGID= rmp->mp_realuid;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			/* Wakeup the original caller */
			setreply(rmp-mproc, OK);
			break;

		case PM_FORK:
		{
			int parent_p;
			struct mproc *parent_mp;

			parent_p = rmp->mp_parent;
			parent_mp = &mproc[parent_p];

			m.m_type= call;
			m.PM_FORK_PPROC= parent_mp->mp_endpoint;
			m.PM_FORK_CPROC= rmp->mp_endpoint;
			m.PM_FORK_CPID= rmp->mp_pid;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			/* Wakeup the newly created process */
			setreply(rmp-mproc, OK);

			/* Wakeup the parent */
			setreply(parent_mp-mproc, rmp->mp_pid);
			break;
		}

		case PM_EXIT:
		case PM_EXIT_TR:
			m.m_type= call;
			m.PM_EXIT_PROC= rmp->mp_endpoint;

			/* Mark the process as busy */
			rmp->mp_fs_call= PM_BUSY;

			break;

		case PM_UNPAUSE:
			m.m_type= call;
			m.PM_UNPAUSE_PROC= rmp->mp_endpoint;

			/* FS does not reply */
			rmp->mp_fs_call2= PM_IDLE;

			/* Ask the kernel to deliver the signal */
			r= sys_sigsend(rmp->mp_endpoint,
				&rmp->mp_sigmsg);
			if (r != OK)
				panic(__FILE__,"sys_sigsend failed",r);

			break;

		case PM_UNPAUSE_TR:
			m.m_type= call;
			m.PM_UNPAUSE_PROC= rmp->mp_endpoint;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			break;

		case PM_EXEC:
			m.m_type= call;
			m.PM_EXEC_PROC= rmp->mp_endpoint;
			m.PM_EXEC_PATH= rmp->mp_exec_path;
			m.PM_EXEC_PATH_LEN= rmp->mp_exec_path_len;
			m.PM_EXEC_FRAME= rmp->mp_exec_frame;
			m.PM_EXEC_FRAME_LEN= rmp->mp_exec_frame_len;

			/* Mark the process as busy */
			rmp->mp_fs_call= PM_BUSY;

			break;

		case PM_FORK_NB:
		{
			int parent_p;
			struct mproc *parent_mp;

			parent_p = rmp->mp_parent;
			parent_mp = &mproc[parent_p];

			m.m_type= PM_FORK;
			m.PM_FORK_PPROC= parent_mp->mp_endpoint;
			m.PM_FORK_CPROC= rmp->mp_endpoint;
			m.PM_FORK_CPID= rmp->mp_pid;

			/* FS does not reply */
			rmp->mp_fs_call= PM_IDLE;

			break;
		}

		case PM_DUMPCORE:
			m.m_type= call;
			m.PM_CORE_PROC= rmp->mp_endpoint;
			m.PM_CORE_SEGPTR= (char *)rmp->mp_seg;

			/* Mark the process as busy */
			rmp->mp_fs_call= PM_BUSY;

			break;

		default:
			printf("send_work: should report call 0x%x to FS\n",
				call);
			break;
		}
		break;
	}
	if (m.m_type != PM_IDLE)
	{
		if (rmp->mp_fs_call == PM_IDLE &&
			rmp->mp_fs_call2 == PM_IDLE &&
			(rmp->mp_flags & PM_SIG_PENDING))
		{
			rmp->mp_flags &= ~PM_SIG_PENDING;
			check_pending(rmp);
			if (!(rmp->mp_flags & PM_SIG_PENDING))
			{
				/* Allow the process to be scheduled */
				sys_nice(rmp->mp_endpoint, rmp->mp_nice);
			}
		}
	}
	else if (report_reboot)
	{
		m.m_type= PM_REBOOT;
		report_reboot= FALSE;
	}
	r= send(FS_PROC_NR, &m);
	if (r != OK) panic("pm", "send_work: send failed", r);
}

PRIVATE void handle_fs_reply(m_ptr)
message *m_ptr;
{
	int r, proc_e, proc_n;
	struct mproc *rmp;

	switch(m_ptr->m_type)
	{
	case PM_EXIT_REPLY:
	case PM_EXIT_REPLY_TR:
		proc_e= m_ptr->PM_EXIT_PROC;
		if (pm_isokendpt(proc_e, &proc_n) != OK)
		{
			panic(__FILE__,
				"PM_EXIT_REPLY: got bad endpoint from FS",
				proc_e);
		}
		rmp= &mproc[proc_n];

		/* Call is finished */
		rmp->mp_fs_call= PM_IDLE;

		if (!(rmp->mp_flags & PRIV_PROC))
		{
			/* destroy the (user) process */
			if((r=sys_exit(proc_e)) != OK)
			{
				panic(__FILE__,
					"PM_EXIT_REPLY: sys_exit failed", r);
			}
		}

		/* Release the memory occupied by the child. */
		if (find_share(rmp, rmp->mp_ino, rmp->mp_dev,
			rmp->mp_ctime) == NULL) {
			/* No other process shares the text segment,
			 * so free it.
			 */
			free_mem(rmp->mp_seg[T].mem_phys,	
				rmp->mp_seg[T].mem_len);
		}
		/* Free the data and stack segments. */
		free_mem(rmp->mp_seg[D].mem_phys, rmp->mp_seg[S].mem_vir +
			rmp->mp_seg[S].mem_len - rmp->mp_seg[D].mem_vir);

		if (m_ptr->m_type == PM_EXIT_REPLY_TR &&
			rmp->mp_parent != INIT_PROC_NR)
		{
			/* Wake up the parent */
			mproc[rmp->mp_parent].mp_reply.reply_trace = 0;
			setreply(rmp->mp_parent, OK);
		}

		/* Clean up if the parent has collected the exit
		 * status
		 */
		if (rmp->mp_flags & TOLD_PARENT)
			real_cleanup(rmp);

		break;

	case PM_REBOOT_REPLY:
	{
		vir_bytes code_addr;
		size_t code_size;

		/* Ask the kernel to abort. All system services, including
		 * the PM, will get a HARD_STOP notification. Await the
		 * notification in the main loop.
		 */
		code_addr = (vir_bytes) monitor_code;
		code_size = strlen(monitor_code) + 1;
		sys_abort(abort_flag, PM_PROC_NR, code_addr, code_size);
		break;
	}

	case PM_EXEC_REPLY:
		proc_e= m_ptr->PM_EXEC_PROC;
		if (pm_isokendpt(proc_e, &proc_n) != OK)
		{
			panic(__FILE__,
				"PM_EXIT_REPLY: got bad endpoint from FS",
				proc_e);
		}
		rmp= &mproc[proc_n];

		/* Call is finished */
		rmp->mp_fs_call= PM_IDLE;

		exec_restart(rmp, m_ptr->PM_EXEC_STATUS);

		if (rmp->mp_flags & PM_SIG_PENDING)
		{
			printf("handle_fs_reply: restarting signals\n");
			rmp->mp_flags &= ~PM_SIG_PENDING;
			check_pending(rmp);
			if (!(rmp->mp_flags & PM_SIG_PENDING))
			{
				printf("handle_fs_reply: calling sys_nice\n");
				/* Allow the process to be scheduled */
				sys_nice(rmp->mp_endpoint, rmp->mp_nice);
			}
			else
				printf("handle_fs_reply: more signals\n");
		}
		break;

	case PM_CORE_REPLY:
	{
		int parent_waiting, right_child;
		pid_t pidarg;
		struct mproc *p_mp;

		proc_e= m_ptr->PM_CORE_PROC;
		if (pm_isokendpt(proc_e, &proc_n) != OK)
		{
			panic(__FILE__,
				"PM_EXIT_REPLY: got bad endpoint from FS",
				proc_e);
		}
		rmp= &mproc[proc_n];

		if (m_ptr->PM_CORE_STATUS == OK)
			rmp->mp_sigstatus |= DUMPED;

		/* Call is finished */
		rmp->mp_fs_call= PM_IDLE;

		p_mp = &mproc[rmp->mp_parent];		/* process' parent */
		pidarg = p_mp->mp_wpid;		/* who's being waited for? */
		parent_waiting = p_mp->mp_flags & WAITING;
		right_child =		/* child meets one of the 3 tests? */
			(pidarg == -1 || pidarg == rmp->mp_pid ||
			-pidarg == rmp->mp_procgrp);

		if (parent_waiting && right_child) {
			tell_parent(rmp);		/* tell parent */
		} else {
			/* parent not waiting, zombify child */
			rmp->mp_flags &= (IN_USE|PRIV_PROC);
			rmp->mp_flags |= ZOMBIE;
			/* send parent a "child died" signal */
			sig_proc(p_mp, SIGCHLD);
		}

		if (!(rmp->mp_flags & PRIV_PROC))
		{
			/* destroy the (user) process */
			if((r=sys_exit(proc_e)) != OK)
			{
				panic(__FILE__,
					"PM_CORE_REPLY: sys_exit failed", r);
			}
		}

		/* Release the memory occupied by the child. */
		if (find_share(rmp, rmp->mp_ino, rmp->mp_dev,
			rmp->mp_ctime) == NULL) {
			/* No other process shares the text segment,
			 * so free it.
			 */
			free_mem(rmp->mp_seg[T].mem_phys,	
				rmp->mp_seg[T].mem_len);
		}
		/* Free the data and stack segments. */
		free_mem(rmp->mp_seg[D].mem_phys, rmp->mp_seg[S].mem_vir +
			rmp->mp_seg[S].mem_len - rmp->mp_seg[D].mem_vir);

		/* Clean up if the parent has collected the exit
		 * status
		 */
		if (rmp->mp_flags & TOLD_PARENT)
			real_cleanup(rmp);

		break;
	}
	default:
		panic(__FILE__, "handle_fs_reply: unknown reply type",
			m_ptr->m_type);
		break;
	}

}

