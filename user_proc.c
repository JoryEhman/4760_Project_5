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

    key_t shmkey = ftok(SHM_KEY_PATH, SHM_KEY_ID);
    if (shmkey == -1) { perror("ftok shm"); return 1; }

    key_t msgkey = ftok(SHM_KEY_PATH, MQ_KEY_ID);
    if (msgkey == -1) { perror("ftok msg"); return 1; }

    int shmid = shmget(shmkey, sizeof(SimClock), 0);
    if (shmid == -1) { perror("shmget"); return 1; }

    SimClock *simClock = (SimClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) { perror("shmat"); return 1; }

    int msqid = msgget(msgkey, 0);
    if (msqid == -1) { perror("msgget"); return 1; }

    printf("user_proc: attached to IPC, clock at %u:%u\n",
           simClock->seconds, simClock->nanoseconds);

    shmdt(simClock);
    return 0;
}