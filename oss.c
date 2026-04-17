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
#include "shared.h"

SimClock *simClock = NULL;
int shmid = -1;
int msqid = -1;

static void cleanup(int sig) {
    (void)sig;
    if (simClock) shmdt(simClock);
    if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
    if (msqid != -1) msgctl(msqid, IPC_RMID, NULL);
    exit(0);
}

int main(void) {

    signal(SIGINT, cleanup);
    signal(SIGALRM, cleanup);
    alarm(5);

    key_t shmkey = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shmkey == -1) { perror("ftok shm"); return 1; }

    key_t msgkey = ftok(SHM_KEY_PATH, MQ_KEY_ID);
    if (msgkey == -1) { perror("ftok msg"); return 1; }

    shmid = shmget(shmkey, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) { perror("shmget"); return 1; }

    simClock = (SimClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) { perror("shmat"); return 1; }

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    msqid = msgget(msgkey, IPC_CREAT | 0666);
    if (msqid == -1) { perror("msgget"); return 1; }

    printf("OSS: IPC initialized successfully\n");
    printf("OSS: Clock at %u:%u\n", simClock->seconds, simClock->nanoseconds);

    cleanup(0);
    return 0;
}
