/*
 * user_proc.c
 * Author: ehman
 * Date: 2026-04-17
 * Environment: Linux, gcc
 * Description: User process for OSS resource management (Project 5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include "shared.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "user_proc: usage: user_proc <maxSeconds>\n");
        return 1;
    }

    int maxSeconds = atoi(argv[1]);

    /* ── Attach to shared memory ── */
    key_t shmkey = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shmkey == -1) { perror("ftok shm"); return 1; }

    int shmid = shmget(shmkey, sizeof(SimClock), 0);
    if (shmid == -1) { perror("shmget"); return 1; }

    SimClock *simClock = (SimClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) { perror("shmat"); return 1; }

    /* ── Attach to message queue ── */
    key_t msgkey = ftok(SHM_KEY_PATH, MQ_KEY_ID);
    if (msgkey == -1) { perror("ftok msg"); return 1; }

    int msqid = msgget(msgkey, 0);
    if (msqid == -1) { perror("msgget"); return 1; }

    pid_t myPid     = getpid();
    pid_t parentPid = getppid();

    /* Record birth time */
    unsigned int birthSec  = simClock->seconds;
    unsigned int birthNano = simClock->nanoseconds;

    printf("user_proc PID:%d attached. maxSeconds=%d birthTime=%u:%u\n",
           myPid, maxSeconds, birthSec, birthNano);

    /* ── Main loop ── */
    while (1) {

        /* Wait for message from OSS */
        msgbuffer msg;
        if (msgrcv(msqid, &msg, sizeof(msgbuffer) - sizeof(long),
                   (long)myPid, 0) == -1) {
            perror("msgrcv");
            break;
        }

        /* OSS told us to terminate */
        if (msg.intData == 0) break;

        /* Check if our time is up */
        unsigned int elapsed = simClock->seconds - birthSec;
        if ((int)elapsed >= maxSeconds) {
            printf("user_proc PID:%d time up at %u:%u, terminating\n",
                   myPid, simClock->seconds, simClock->nanoseconds);

            /* Send termination message */
            msgbuffer reply;
            reply.mtype   = (long)parentPid;
            reply.intData = 0;
            msgsnd(msqid, &reply, sizeof(msgbuffer) - sizeof(long), 0);
            break;
        }

        /* Still running — send back intData=1 */
        printf("user_proc PID:%d still running at %u:%u\n",
               myPid, simClock->seconds, simClock->nanoseconds);

        msgbuffer reply;
        reply.mtype   = (long)parentPid;
        reply.intData = 1;
        msgsnd(msqid, &reply, sizeof(msgbuffer) - sizeof(long), 0);
    }

    shmdt(simClock);
    return 0;
}