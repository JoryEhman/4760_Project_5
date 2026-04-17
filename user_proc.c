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
#include <time.h>
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

    /* Seed RNG uniquely per process */
    srand((unsigned)(time(NULL) ^ ((unsigned long)getpid() * 2654435761UL)));

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

    /* Track what resources we currently hold */
    int myResources[NUM_RESOURCES];
    for (int i = 0; i < NUM_RESOURCES; i++)
        myResources[i] = 0;

    /* Record birth time */
    unsigned int birthSec = simClock->seconds;

    /* ── Main loop ── */
    while (1) {

        /* Wait for message from OSS */
        msgbuffer msg;
        if (msgrcv(msqid, &msg, sizeof(msgbuffer) - sizeof(long),
                   (long)myPid, 0) == -1) {
            perror("msgrcv");
            break;
        }

        /* OSS told us to terminate immediately */
        if (msg.intData == 0) break;

        /* Check if our time is up */
        unsigned int elapsed = simClock->seconds - birthSec;
        if ((int)elapsed >= maxSeconds) {
            /* Send termination message (0) */
            msgbuffer reply;
            reply.mtype   = (long)parentPid;
            reply.intData = 0;
            msgsnd(msqid, &reply, sizeof(msgbuffer) - sizeof(long), 0);
            break;
        }

        /* ── Decide: request (70%) or release (30%) ── */
        int doRequest = ((rand() % 100) < 70);

        if (doRequest) {
            /* Pick a random resource to request */
            int r = rand() % NUM_RESOURCES;

            /* Never hold more than INSTANCES_PER_RESOURCE of one resource */
            if (myResources[r] >= INSTANCES_PER_RESOURCE) {
                /* At cap for this one — try releasing instead */
                doRequest = 0;
            } else {
                /* Send request: positive value = resource index + 1 */
                msgbuffer reply;
                reply.mtype   = (long)parentPid;
                reply.intData = r + 1;
                msgsnd(msqid, &reply, sizeof(msgbuffer) - sizeof(long), 0);

                /* Wait for grant — OSS will reply when resource is available */
                msgbuffer grant;
                if (msgrcv(msqid, &grant, sizeof(msgbuffer) - sizeof(long),
                           (long)myPid, 0) == -1) {
                    perror("msgrcv grant");
                    break;
                }

                /* Resource granted */
                myResources[r]++;
                continue;
            }
        }

        if (!doRequest) {
            /* Find a resource we actually hold */
            int r = -1;
            for (int tries = 0; tries < NUM_RESOURCES * 2; tries++) {
                int candidate = rand() % NUM_RESOURCES;
                if (myResources[candidate] > 0) {
                    r = candidate;
                    break;
                }
            }

            if (r == -1) {
                /* Nothing to release — request something instead */
                int r2 = rand() % NUM_RESOURCES;
                if (myResources[r2] < INSTANCES_PER_RESOURCE) {
                    msgbuffer reply;
                    reply.mtype   = (long)parentPid;
                    reply.intData = r2 + 1;
                    msgsnd(msqid, &reply,
                           sizeof(msgbuffer) - sizeof(long), 0);

                    msgbuffer grant;
                    if (msgrcv(msqid, &grant,
                               sizeof(msgbuffer) - sizeof(long),
                               (long)myPid, 0) == -1) {
                        perror("msgrcv grant2");
                        break;
                    }
                    myResources[r2]++;
                }
                continue;
            }

            /* Send release: negative value = -(resource index + 1) */
            msgbuffer reply;
            reply.mtype   = (long)parentPid;
            reply.intData = -(r + 1);
            msgsnd(msqid, &reply, sizeof(msgbuffer) - sizeof(long), 0);
            myResources[r]--;

            /* Wait for OSS acknowledgement */
            msgbuffer ack;
            if (msgrcv(msqid, &ack, sizeof(msgbuffer) - sizeof(long),
                       (long)myPid, 0) == -1) {
                perror("msgrcv ack");
                break;
            }
        }
    }

    shmdt(simClock);
    return 0;
}