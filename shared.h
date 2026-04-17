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

#define MAX_PROCS              20
#define NUM_RESOURCES          10
#define INSTANCES_PER_RESOURCE  5
#define MAX_LOG_LINES       10000
#define BILLION          1000000000ULL

#define SHM_KEY_PATH  "oss.c"
#define SHM_KEY_ID    1
#define MQ_KEY_ID     2

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

typedef struct {
    long mtype;
    int  intData;
} msgbuffer;

#endif