#include <stdio.h>
#include <proc/readproc.h>
#include <assert.h>
#include <malloc.h>
#include <sys/types.h>
#include <signal.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ptrace.h>

/* Compile this with option -lprocps.  It needs libprocps-dev package.
 * Don't forget the -DMAXTEMP=XXX */

#ifndef MAXTEMP
#define MAXTEMP 115000
#endif

static int nprocs = 0;
#define PROC_ALREADY_STOPPED 1
#define PROC_SPECIAL 2
#define PROC_ROOT 4
static struct {
  pid_t pid, ppid;
  pid_t *children;
  int nchildren;
  int flags;
} *procs;

static void insert_proc(pid_t pid, pid_t ppid, int flags) {
	int i;

	/* Check if parent already inserted ... */
	if (flags != PROC_ROOT) {
		for (i = 0; i < nprocs; i++) if (procs[i].pid == ppid) break;
		if (i == nprocs) insert_proc(ppid, 0, PROC_ROOT);
		procs[i].children = realloc(procs[i].children, 
		  sizeof(pid_t) * (procs[i].nchildren + 1));
		procs[i].children[procs[i].nchildren++] = pid;
	}

	/* Check if pid already inserted ... */
	for (i = 0; i < nprocs; i++) if (procs[i].pid == pid) break;
	if (i == nprocs) { /* ... not found, add */
		procs = realloc(procs, (nprocs + 1) * sizeof(*procs));
		procs[nprocs].pid = pid;
		procs[nprocs].children = NULL;
		procs[nprocs].nchildren = 0;
		nprocs++;
	}
	procs[i].flags = flags;
	procs[i].ppid = ppid;
}

static pid_t *kill_list = NULL;
static int nkills = 0;
static void recurse(pid_t pid) {
	int i, j;

	for (i = 0; i < nprocs; i++) if (procs[i].pid == pid) break; 
	if (i == nprocs) return; /* not found */

	for (j = 0; j < procs[i].nchildren; j++) 
		if (!(procs[i].flags & PROC_SPECIAL))
			recurse(procs[i].children[j]);

	if (!procs[i].flags & PROC_ALREADY_STOPPED) kill_list[nkills++] = pid;

}

static void idle_inject(void) {
	struct sched_param sched;
	PROCTAB *pt;
	proc_t *p;
	pid_t self;
	int i;

	if (kill_list != NULL) return;

	sched.sched_priority = 99;
	sched_setscheduler(0, SCHED_FIFO, &sched);

	self = getpid();
	pt = openproc(PROC_FILLSTATUS|PROC_FILLSTAT|PROC_FILLCOM);
	assert(pt != NULL);
	i = 0;
	while ((p = readproc(pt, NULL)) != NULL) {
		int flags = 0;
		if (p->state == 'T') flags = PROC_ALREADY_STOPPED; 
		if (!p->cmdline || p->cmdline[0][0] == '@' || p->tid == self) 
		  flags |= PROC_SPECIAL;
		insert_proc(p->tid, p->ppid, flags);
		freeproc(p);
	}
	closeproc(pt);
	kill_list = malloc(sizeof(pid_t) * nprocs);
	nkills = 0;
	for (i = 0; i < nprocs; i++) 
	  if (procs[i].flags & PROC_ROOT) recurse(procs[i].pid);
	for (i = (nkills - 1); i >= 0; i--) {
//		sending SIGSTOP had side-effect of sending SIGCHLD 
//		to parent process randomly, use ptrace instead
		ptrace(PTRACE_SEIZE, kill_list[i], 0, 0);
		ptrace(PTRACE_INTERRUPT, kill_list[i], 0, 0);
	}
}

static void idle_cancel(void) {
	int i;
	struct sched_param sched;

	if (kill_list == NULL) return;

	for (i = 0; i < nkills; i++) {
		ptrace(PTRACE_DETACH, kill_list[i], 0, 0);
	}

	sched.sched_priority = 0;
	sched_setscheduler(0, SCHED_OTHER, &sched);
	free(kill_list);
	for (i = 0; i < nprocs; i++) 
		if (procs[i].children) free(procs[i].children);
	free(procs);
	procs = NULL;
	kill_list = NULL;
	nprocs = 0;
	nkills = 0;
}

static int millicelsius(void) {
	int t = 0;
	FILE *s = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
	if (s != NULL) {
		if (fscanf(s, "%d", &t) == 0) t = 0;
		fclose(s);
		fprintf(stderr, "%d\n", t);
	}
	return t;
}

int main(int argc, char **argv) {

	atexit(idle_cancel);

	for (;;) {
		if (millicelsius() >= MAXTEMP) idle_inject(); 
		else idle_cancel(); 
		usleep(100000); /* 1/10th of a second */
	}
}
