/* 
 * tsh - A tiny shell program with job control
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* Per-job data */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, FG, BG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

volatile sig_atomic_t ready; /* Is the newest child in its own process group? */

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);
void sigusr1_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int freejid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != -1) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    Signal(SIGUSR1, sigusr1_handler); /* Child is ready */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
    // start with if the user has not requested built-in command
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int bg;
    pid_t pid;
    sigset_t mask, prev;;



    if (!builtin_cmd(argv)) {

        // blocking thrown signals
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        pid = fork();

        //child process
        if (pid == 0) {
                setpgid(0, 0); // putting the child in a group pid
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
        }

        //parent process
        else {
                //use the parseline function - it returns True when

                if (!bg) {
                       addjob(jobs, pid, FG, cmdline);
                       sigprocmask(SIG_UNBLOCK, &mask, NULL);
                       waitfg(pid); // waits for the fg process to terminate
                       // printing information
                } else { // running in backgorund
                     addjob(jobs, pid, BG, cmdline);
                     sigprocmask(SIG_UNBLOCK, &mask, NULL);
                     int j = pid2jid(pid);
                     // we should print information about this job
                     printf("[%d] (%d) %s", j, pid, cmdline);


                       // running in foreground
                }
        }

    }
}
/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return number of arguments parsed.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to space or quote delimiters */
    int argc;                   /* number of args */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    return argc;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
   // Iterate through the arguments
        if (!strcmp(argv[0], "quit")  == 0) {
                   exit(EXIT_SUCCESS);
        }
        else if (!strcmp(argv[0], "fg") == 0) {
                   do_bgfg(argv);
                   return 1;
        }
        else if (!strcmp(argv[0], "bg") == 0) {
                   do_bgfg(argv);
                   return 1;
                   }
        else if (!strcmp(argv[0], "jobs") == 0){
                   listjobs(jobs);
                   return 1;
        }
   return 0;     /* not a builtin command */
   }

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {

        pid_t pid;
        struct job_t *job;
        char *arg = argv[1];

        if (arg == NULL) {
                fprintf(stderr, "PID or JID required");
                exit(1);
        }

        else if(arg[0] == '%'){ // if it's a jid, get the job using jid

           int jid; // convert into int jid to use getjobjid()
           jid = atoi(&arg[1]);
                // find job by jid

           if (jid == 0) {
                printf("Argument must be a PID or JID");
                exit(1);
           }

           // conversion was successfull, getting the job now, using jid
           job = getjobjid(jobs, jid);

           // we have the job, we can get the pid
           if (job != NULL) {
                  pid_t pid_for_jid = job -> pid;// using arrow to access the pid from the struct
           }
           else {

                printf("(%s), does not exist -  argument must be PID or JID", argv[1]);
                exit(1);
           }
        }
        else { //if it's a pid
                pid = atoi(arg); // converting the string to int

                //error checking, if atoi() was unsuccessfull

                if (pid == 0) {
                        printf("Argument must be a PID or JID");
                        exit(1);
                }
                job = getjobpid(jobs, pid);
                if (job == NULL) {
                        printf("%d, does not exist", pid);
                }

        }
        // job is defined in both cases, so can use it
        pid = job -> pid;

        // sending a SIGCONT to the  process every time
        // check if they typed bg or fg
        kill(-pid, SIGCONT);
        if (!strcmp(argv[0], "bg")) {
                // change state to bg and print info
                job -> state = BG;

                printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);

        }

        else if (!strcmp(argv[0], "fg")) {
                job -> state = FG;
                waitfg(job -> pid); // in every case of fg, we want to wait for fg to terminate
        }
        return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    // the below function gives the group process id of the pid

    struct job_t *job;

    if (pid == 0) {
            return;
    }

    else {
            job = getjobpid(jobs, pid);
            if (job == NULL) {
                printf("Invalid job!");
            }
            else {
                while(pid == fgpid(jobs)) {
                        sleep(1);
                }
            }



    }
    // while(1) {
    //     if (pgid == fgpgid) {
    //          sleep(1);
    //      }else {
    //          break;
    //      }
    return;
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    // we have to call waitpid to learn about the child stopping
        sigset_t mask, prev; //going to use this for signal blocking and unblocking;
        pid_t pid;
        int status;
        struct job_t *job;


        while ((pid = waitpid(-1, &status, WNOHANG))> 0)        {       // pid of -1 means I wait for child to finish
                job = getjobpid(jobs, pid);
                //Analyzing a child's process exit status
                //
                sigprocmask(SIG_BLOCK, &mask, &prev); //Blocking the signal
                if (WIFEXITED(status)) {
                        printf("Child process %d terminated normally with status %d\n", pid, WIFEXITED(status));

                        deletejob(jobs, pid); // deleting the job
                        sigprocmask(SIG_SETMASK, &prev, NULL); //Unblocking the signal
                }


                else if (WIFSIGNALED(status)) {
                        int j = pid2jid(pid);
                        printf("Job [%d] (%d) terminated with signal number %d\n", j, pid, WTERMSIG(status));
                        deletejob(jobs, pid);
                        sigprocmask(SIG_BLOCK, &prev, NULL); //Unblocking the signal
                }


                else if (WIFSTOPPED(status)) {
                        int j = pid2jid(pid);
                        printf("Job [%d] (%d) terminated with signal number %d\n", j, pid, WSTOPSIG(status));
                        job -> state = ST;
                        sigprocmask(SIG_BLOCK, &prev, NULL); //Unblocking the signal
                }
        }
        return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
        pid_t pid = fgpid(jobs);
        if (pid == 0) {
                return;
        }
        else if(pid){
                kill(-pid, sig);
        }

    return;
}





        // get pid of the foreground job
            // pid_t fgpid = fgpid(jobs);
             // fgpid() returns zero if no such job exists
            // if (fgpid != 0) {
                // sending it to the foreground job
        //      kill(-fgpid, sig);

          //   }

   //  return;
//}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
        pid_t pid = fgpid(jobs);

        if(pid == 0) {
                return;
        }
        else if(pid){
                kill(-pid, sig);
        }
        getjobpid(jobs,pid)->state = ST;

    return;
}

/*
 * sigusr1_handler - child is ready
 */
void sigusr1_handler(int sig) {
    ready = 1;
}


/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* freejid - Returns smallest free job ID */
int freejid(struct job_t *jobs) {
    int i;
    int taken[MAXJOBS + 1] = {0};
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid != 0) 
        taken[jobs[i].jid] = 1;
    for (i = 1; i <= MAXJOBS; i++)
        if (!taken[i])
            return i;
    return 0;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    
    if (pid < 1)
        return 0;
    int free = freejid(jobs);
    if (!free) {
        printf("Tried to create too many jobs\n");
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = free;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    return 0; /*suppress compiler warning*/
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
    }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message and terminate
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}


