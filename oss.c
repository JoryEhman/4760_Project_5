/*
 * oss.c
 * Author: ehman
 * Date: 2026-04-17
 * Environment: Linux, gcc
 * Description: OSS resource management simulator (Project 5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdarg.h>
#include "shared.h"

/* ── Schedule one unblocked process (round-robin) ── */
static int lastScheduled = 0;

/* ── Globals ─────────────────────────────────────────────────────────────── */
SimClock *simClock = NULL;
int       shmid    = -1;
int       msqid    = -1;
FILE     *logfp    = NULL;
int       logLines =  0;

PCB      processTable[MAX_PROCS];
Resource resourceTable[NUM_RESOURCES];

/* ── Logging ─────────────────────────────────────────────────────────────── */
static void logWrite(const char *fmt, ...) {
    if (logLines >= MAX_LOG_LINES) return;

    va_list args;
    char buf[1024];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    for (char *p = buf; *p; p++)
        if (*p == '\n') logLines++;

    printf("%s", buf);
    if (logfp) {
        fprintf(logfp, "%s", buf);
        fflush(logfp);
    }
}

/* ── Cleanup ─────────────────────────────────────────────────────────────── */
static void cleanup(int sig) {
    (void)sig;

    for (int i = 0; i < MAX_PROCS; i++)
        if (processTable[i].occupied)
            kill(processTable[i].pid, SIGTERM);

    while (waitpid(-1, NULL, WNOHANG) > 0);

    if (simClock) shmdt(simClock);
    if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
    if (msqid != -1) msgctl(msqid, IPC_RMID, NULL);
    if (logfp)  fclose(logfp);

    exit(0);
}

/* ── Advance clock by given nanoseconds ──────────────────────────────────── */
static void advanceClock(unsigned int ns) {
    simClock->nanoseconds += ns;
    while (simClock->nanoseconds >= BILLION) {
        simClock->seconds++;
        simClock->nanoseconds -= BILLION;
    }
}

/* ── Find free slot in process table ────────────────────────────────────── */
static int findFreeSlot(void) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (!processTable[i].occupied) return i;
    return -1;
}

/* ── Count active processes ──────────────────────────────────────────────── */
static int countActive(void) {
    int count = 0;
    for (int i = 0; i < MAX_PROCS; i++)
        if (processTable[i].occupied) count++;
    return count;
}

/* ── Launch a child process ──────────────────────────────────────────────── */
static int launchChild(int slot, int t) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        char tStr[16];
        snprintf(tStr, sizeof(tStr), "%d", t);
        execl("./user_proc", "user_proc", tStr, NULL);
        perror("execl");
        exit(1);
    }

    /* Parent: fill PCB */
    processTable[slot].occupied          = 1;
    processTable[slot].pid               = pid;
    processTable[slot].startSeconds      = (int)simClock->seconds;
    processTable[slot].startNano         = (int)simClock->nanoseconds;
    processTable[slot].blocked           = 0;
    processTable[slot].requestedResource = -1;
    for (int r = 0; r < NUM_RESOURCES; r++)
        processTable[slot].resourcesAllocated[r] = 0;

    logWrite("OSS: Launched user_proc PID %d in slot %d at %u:%09u\n",
             pid, slot, simClock->seconds, simClock->nanoseconds);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {

    /* ── Default options ── */
    int    n     = 18;
    int    s     = 18;
    int    t     = 5;
    double i_val = 0.5;
    char   logfile[256] = "oss.log";

    /* ── Parse arguments ── */
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timeLimit]"
                       " [-i fraction] [-f logfile]\n", argv[0]);
                return 0;
            case 'n': n     = atoi(optarg); break;
            case 's': s     = atoi(optarg); break;
            case 't': t     = atoi(optarg); break;
            case 'i': i_val = atof(optarg); break;
            case 'f': strncpy(logfile, optarg, sizeof(logfile) - 1); break;
            default:
                fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul]"
                        " [-t timeLimit] [-i fraction] [-f logfile]\n",
                        argv[0]);
                return 1;
        }
    }

    /* ── Enforce limits ── */
    if (s > MAX_RUNNING) s = MAX_RUNNING;
    if (s < 1) s = 1;
    if (n < 1) n = 1;
    if (t < 1) t = 1;

    /* ── Signal handlers ──
     * Use sigaction with no SA_RESTART so that msgrcv() is interrupted
     * by SIGALRM, allowing the 5-second limit to actually terminate us */
    struct sigaction sa;
    sa.sa_handler = cleanup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* no SA_RESTART */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);
    alarm(5);

    /* ── Open log file ── */
    logfp = fopen(logfile, "w");
    if (!logfp) { perror("fopen"); return 1; }

    /* ── IPC setup ── */
    key_t shmkey = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shmkey == -1) { perror("ftok shm"); cleanup(0); return 1; }

    key_t msgkey = ftok(SHM_KEY_PATH, MQ_KEY_ID);
    if (msgkey == -1) { perror("ftok msg"); cleanup(0); return 1; }

    shmid = shmget(shmkey, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) { perror("shmget"); cleanup(0); return 1; }

    simClock = (SimClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) { perror("shmat"); cleanup(0); return 1; }

    simClock->seconds     = 0;
    simClock->nanoseconds = 0;

    msqid = msgget(msgkey, IPC_CREAT | 0666);
    if (msqid == -1) { perror("msgget"); cleanup(0); return 1; }

    /* ── Initialize process table ── */
    for (int i = 0; i < MAX_PROCS; i++) {
        processTable[i].occupied          = 0;
        processTable[i].pid               = 0;
        processTable[i].blocked           = 0;
        processTable[i].requestedResource = -1;
        for (int r = 0; r < NUM_RESOURCES; r++)
            processTable[i].resourcesAllocated[r] = 0;
    }

    /* ── Initialize resource table ── */
    for (int r = 0; r < NUM_RESOURCES; r++) {
        resourceTable[r].total     = INSTANCES_PER_RESOURCE;
        resourceTable[r].available = INSTANCES_PER_RESOURCE;
    }

    logWrite("OSS: Starting. n=%d s=%d t=%d i=%.2f logfile=%s\n",
             n, s, t, i_val, logfile);

    /* ── Statistics ── */
    int totalRequests      = 0;
    int grantedImmediately = 0;

    /* ── Launch timing ── */
    int launched = 0;
    unsigned long long lastLaunchNs  = 0;
    unsigned long long intervalNs    =
        (unsigned long long)(i_val * (double)BILLION);

    /* ── Half-second table print tracking ── */
    unsigned long long lastPrintNs = 0;

    /* ── Main loop ── */
    while (launched < n || countActive() > 0) {

        /* Current simulated time in ns */
        unsigned long long nowNs =
            (unsigned long long)simClock->seconds * BILLION +
            simClock->nanoseconds;

        /* ── Try to launch a new child ── */
        if (launched < n && countActive() < s) {
            if (launched == 0 || nowNs - lastLaunchNs >= intervalNs) {
                int slot = findFreeSlot();
                if (slot != -1) {
                    if (launchChild(slot, t) == 0) {
                        launched++;
                        lastLaunchNs = nowNs;
                    }
                }
            }
        }

        /* ── Advance clock by 10ms ── */
        advanceClock(10000000);
        nowNs = (unsigned long long)simClock->seconds * BILLION +
                simClock->nanoseconds;

        /* ── Unblock any waiting process whose resource is now free ── */
        for (int i = 0; i < MAX_PROCS; i++) {
            if (!processTable[i].occupied) continue;
            if (!processTable[i].blocked)  continue;

            int r = processTable[i].requestedResource;
            if (r >= 0 && resourceTable[r].available > 0) {
                resourceTable[r].available--;
                processTable[i].resourcesAllocated[r]++;
                processTable[i].blocked           = 0;
                processTable[i].requestedResource = -1;

                logWrite("OSS: Unblocking P%d, granting R%d at %u:%09u\n",
                         i, r, simClock->seconds, simClock->nanoseconds);

                msgbuffer grantMsg;
                grantMsg.mtype   = (long)processTable[i].pid;
                grantMsg.intData = 1;
                msgsnd(msqid, &grantMsg,
                       sizeof(msgbuffer) - sizeof(long), 0);
            }
        }

        /* ── Schedule one unblocked process (round-robin) ── */
        int scheduled = 0;
        for (int count = 0; count < MAX_PROCS && !scheduled; count++) {
            int i = (lastScheduled + count) % MAX_PROCS;
            if (!processTable[i].occupied) continue;
            if (processTable[i].blocked)   continue;

            lastScheduled = (i + 1) % MAX_PROCS;
            scheduled = 1;

            msgbuffer msg;
            msg.mtype   = (long)processTable[i].pid;
            msg.intData = 1;

            logWrite("OSS: Sending message to P%d PID %d at %u:%09u\n",
                     i, processTable[i].pid,
                     simClock->seconds, simClock->nanoseconds);

            msgsnd(msqid, &msg, sizeof(msgbuffer) - sizeof(long), 0);

            /* Wait for reply — SIGALRM will interrupt this if time expires */
            msgbuffer reply;
            if (msgrcv(msqid, &reply, sizeof(msgbuffer) - sizeof(long),
                       getpid(), 0) == -1) {
                if (errno == EINTR) cleanup(0);
                perror("msgrcv");
                cleanup(0);
            }

            int val = reply.intData;

            if (val == 0) {
                logWrite("OSS: P%d PID %d terminating at %u:%09u\n",
                         i, processTable[i].pid,
                         simClock->seconds, simClock->nanoseconds);

                for (int r = 0; r < NUM_RESOURCES; r++) {
                    resourceTable[r].available +=
                        processTable[i].resourcesAllocated[r];
                    processTable[i].resourcesAllocated[r] = 0;
                }

                waitpid(processTable[i].pid, NULL, 0);
                processTable[i].occupied = 0;

            } else if (val > 0) {
                int r = val - 1;
                totalRequests++;

                logWrite("OSS: P%d requesting R%d at %u:%09u\n",
                         i, r, simClock->seconds, simClock->nanoseconds);

                if (resourceTable[r].available > 0) {
                    resourceTable[r].available--;
                    processTable[i].resourcesAllocated[r]++;
                    grantedImmediately++;

                    logWrite("OSS: Granting R%d to P%d at %u:%09u\n",
                             r, i, simClock->seconds, simClock->nanoseconds);

                    msgbuffer grantMsg;
                    grantMsg.mtype   = (long)processTable[i].pid;
                    grantMsg.intData = 1;
                    msgsnd(msqid, &grantMsg,
                           sizeof(msgbuffer) - sizeof(long), 0);
                } else {
                    logWrite("OSS: R%d unavailable, blocking P%d at %u:%09u\n",
                             r, i, simClock->seconds, simClock->nanoseconds);

                    processTable[i].blocked           = 1;
                    processTable[i].requestedResource = r;
                }

            } else {
                int r = (-val) - 1;

                logWrite("OSS: P%d releasing R%d at %u:%09u\n",
                         i, r, simClock->seconds, simClock->nanoseconds);

                if (processTable[i].resourcesAllocated[r] > 0) {
                    processTable[i].resourcesAllocated[r]--;
                    resourceTable[r].available++;
                }

                msgbuffer ack;
                ack.mtype   = (long)processTable[i].pid;
                ack.intData = 1;
                msgsnd(msqid, &ack, sizeof(msgbuffer) - sizeof(long), 0);
            }
        }

        /* ── Print process table every 500ms simulated time ── */
        if (nowNs - lastPrintNs >= 500000000ULL) {
            logWrite("\n--- Process Table @ %u:%09u ---\n",
                     simClock->seconds, simClock->nanoseconds);
            logWrite("%-5s %-8s %-8s %-12s Allocated[R0..R9]\n",
                     "Slot", "PID", "Blocked", "ReqRes");
            for (int i = 0; i < MAX_PROCS; i++) {
                if (!processTable[i].occupied) continue;
                logWrite("%-5d %-8d %-8d %-12d",
                         i, processTable[i].pid,
                         processTable[i].blocked,
                         processTable[i].requestedResource);
                for (int r = 0; r < NUM_RESOURCES; r++)
                    logWrite(" %d",
                             processTable[i].resourcesAllocated[r]);
                logWrite("\n");
            }

            logWrite("\nResources available:\n");
            for (int r = 0; r < NUM_RESOURCES; r++)
                logWrite("R%d:%d ", r, resourceTable[r].available);
            logWrite("\n---\n\n");

            lastPrintNs = nowNs;
        }
    }

    /* ── Final report ── */
    double pct = totalRequests > 0
        ? (double)grantedImmediately / totalRequests * 100.0 : 0.0;

    logWrite("\n=== OSS Final Report ===\n");
    logWrite("Total launched:           %d\n", launched);
    logWrite("Total requests:           %d\n", totalRequests);
    logWrite("Granted immediately:      %d (%.1f%%)\n",
             grantedImmediately, pct);
    logWrite("Final simulated time:     %u:%09u\n",
             simClock->seconds, simClock->nanoseconds);

    cleanup(0);
    return 0;
}