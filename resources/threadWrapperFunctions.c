#include "../../../usr/local/include/dispatch/dispatch.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

dispatch_group_t createGroup() {
    return dispatch_group_create();
}

void asyncDispatch(dispatch_group_t g, void *args, dispatch_function_t func) {
    return dispatch_group_async_f(g, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), args, func);
}

long wait(dispatch_group_t g, long time) {
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, time);
    return dispatch_group_wait(g, timeout);
}

void release(dispatch_group_t g) {
    return dispatch_release(g);
}

void abort() {
    printf("thread execution failed to terminate\n");
    exit(-1);
}

long integerDivide(long a, long b) {
    return (long)floor((double)a/b);
}

int modulo(int i) {
    if(i < 0) {
        return -i;
    }
    else {
        return i;
    }
}

void calcBounds(int threadNo, int totalThreads, int andEqualTo, int leaveIfCondTrue, int phiInCond, int cmpBefore, long totalStart, long totalEnd, long step,  long *start, long *end ) {
    printf("in calc phi bounds\n");
    if(totalThreads > (totalEnd - totalStart)/step) {
        printf("Too many threads assigned to a loop with only %ld iterations\n", (totalEnd - totalStart)/step);
        exit(-1);
    }
    long diff = (totalEnd - totalStart)/(long)totalThreads;
    long jump = step * (diff / step);
    printf("diff = %ld\n", diff);
    int lastThread = 0;
    if(threadNo == (totalThreads - 1)) {
        *end = totalEnd;
        lastThread = 1;
    }
    else {
        *end = totalStart + (threadNo + 1) * jump;
    }
    if(threadNo == 0) {
        *start = totalStart;
    }
    else {
        *start = totalStart + threadNo*jump;
    }

    if(!lastThread) {
        if((phiInCond && !cmpBefore && !leaveIfCondTrue)) {
            (*end) -= step;
        }
    }

    printf("bounds assigned to thread %d : %ld to %ld\n", threadNo, *start, *end);
}

void calcStartValue(int threadNo, int totalThreads, int andEqualTo, int LT, int leaveIfCondTrue, int phiInCond, int cmpBefore, long totalStart, long totalEnd, long inductionstep, int start, int step, int *startVal) {
    printf("In calc start val\n");
    long startc = 0;
    long endc = 0;
    if(threadNo == 0) {
        *startVal = start;
        printf("start val = %d\n", *startVal);
        return;
    }
    long i = 0;
    calcBounds(threadNo - 1, totalThreads, andEqualTo, leaveIfCondTrue, phiInCond, cmpBefore, totalStart, totalEnd, inductionstep, &startc, &endc);
    int counter = 0;
    if(LT) {
        for(i = totalStart; i <= endc; i+=inductionstep) {
            if(i == endc && !andEqualTo) {
                break;
            }
            counter++;
        }
    }
    else {
        for(i = totalStart; i >= endc; i+=inductionstep) {
            if(i == endc && !andEqualTo) {
                break;
            }
             counter++;
        }
    }
    *startVal = start + (counter * step);
    printf("start val = %d\n", *startVal);
}

void print(int i) {
    printf("thread returned value %d\n", i);
}

void printl(long i) {
    printf("thread running iteration %ld\n",i);
}
