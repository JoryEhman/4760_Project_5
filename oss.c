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
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdarg.h>
#include "shared.h"

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

    /* Count newlines */
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

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {

    /* ── Default options ── */
    int  n       = 18;
    int  s       = 18;
    int  t       = 5;
    double i_val = 0.5;
    char logfile[256] = "oss.log";

    /* ── Parse arguments ── */
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timeLimit]"
                       " [-i fraction] [-f logfile]\n", argv[0]);
                return 0;
            case 'n': n     = atoi(optarg);  break;
            case 's': s     = atoi(optarg);  break;
            case 't': t     = atoi(optarg);  break;
            case 'i': i_val = atof(optarg);  break;
            case 'f': strncpy(logfile, optarg, sizeof(logfile) - 1); break;
            default:
                fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul]"
                        " [-t timeLimit] [-i fraction] [-f logfile]\n", argv[0]);
                return 1;
        }
    }

    /* ── Enforce limits ── */
    if (s > MAX_RUNNING) s = MAX_RUNNING;
    if (s < 1) s = 1;
    if (n < 1) n = 1;
    if (t < 1) t = 1;

    /* ── Signal handlers ── */
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGALRM, cleanup);
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
        processTable[i].occupied        = 0;
        processTable[i].pid             = 0;
        processTable[i].blocked         = 0;
        processTable[i].requestedResource = -1;
        for (int r = 0; r < NUM_RESOURCES; r++)
            processTable[i].resourcesAllocated[r] = 0;
    }

    /* ── Initialize resource table ── */
    for (int r = 0; r < NUM_RESOURCES; r++) {
        resourceTable[r].total     = INSTANCES_PER_RESOURCE;
        resourceTable[r].available = INSTANCES_PER_RESOURCE;
    }

    /* ── Confirm startup ── */
    logWrite("OSS: Starting. n=%d s=%d t=%d i=%.2f logfile=%s\n",
             n, s, t, i_val, logfile);
    logWrite("OSS: IPC initialized. Clock at %u:%u\n",
             simClock->seconds, simClock->nanoseconds);
    logWrite("OSS: Resource table initialized (%d resources, %d instances each)\n",
             NUM_RESOURCES, INSTANCES_PER_RESOURCE);

    cleanup(0);
    return 0;
}