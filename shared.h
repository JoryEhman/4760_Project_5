/*
* shared.h
 * Author: ehman
 * Date: 2026-04-17
 * Environment: Linux, gcc
 * Description: Shared definitions for OSS resource management (Project 5)
 */

#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define MAX_PROCS              20
#define MAX_RUNNING            18
#define NUM_RESOURCES          10
#define INSTANCES_PER_RESOURCE  5
#define MAX_LOG_LINES       10000
#define BILLION          1000000000ULL

/* ── IPC keys ────────────────────────────────────────────────────────────── */
#define SHM_KEY_PATH  "oss.c"
#define SHM_KEY_ID    1
#define MQ_KEY_ID     2

/* ── Simulated clock ─────────────────────────────────────────────────────── */
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

/* ── Process Control Block ───────────────────────────────────────────────── */
typedef struct {
    int   occupied;
    pid_t pid;
    int   startSeconds;
    int   startNano;
    int   eventWaitSec;
    int   eventWaitNano;
    int   blocked;
    int   resourcesAllocated[NUM_RESOURCES];
    int   requestedResource;
} PCB;

/* ── Resource Descriptor ─────────────────────────────────────────────────── */
typedef struct {
    int total;
    int available;
} Resource;

/* ── Message buffer ──────────────────────────────────────────────────────── */
/*
 * worker -> oss: intData > 0  = requesting resource (intData - 1)
 *                intData < 0  = releasing resource  ((-intData) - 1)
 *                intData == 0 = terminating
 * oss -> worker: intData == 1 = your turn / granted
 *                intData == 0 = terminate now
 */
typedef struct {
    long mtype;
    int  intData;
} msgbuffer;

#endif