// scheduler.h
//	Data structures for the thread dispatcher and scheduler.
//	Primarily, the list of threads that are ready to run.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "copyright.h"
#include "list.h"
#include "thread.h"

// The following class defines the scheduler/dispatcher abstraction --
// the data structures and operations needed to keep track of which
// thread is running, and which threads are ready but not running.

class Scheduler {
  public:
    Scheduler();  // Initialize list of ready threads
    ~Scheduler(); // De-allocate ready list

    void ReadyToRun(Thread *thread);
    // Thread can be dispatched.
    Thread *FindNextToRun(); // Dequeue first thread on the ready
                             // list, if any, and return thread.
    void Run(Thread *nextThread, bool finishing);
    // Cause nextThread to start running
    void CheckToBeDestroyed(); // Check if thread that had been
                               // running needs to be deleted
    void Print(); // Print contents of ready list
    static int CompareRR(Thread* x, Thread* y);
    static int CompareSJF(Thread* x, Thread* y);
    static int ComparePriority(Thread* x, Thread* y);
    bool isPreemptive();
    void ageUpdate();
    // SelfTest for scheduler is implemented in class Thread

  private:
    SortedList<Thread *> *L1List; // L1 queue, preemptive
    SortedList<Thread *> *L2List; // L2 queue, non-preemptive
    SortedList<Thread *> *L3List; // L3 queue
    enum scheType{RR, SJF, Priority};
    scheType schedulerType;
    Thread *toBeDestroyed; // finishing thread to be destroyed
                           // by the next thread that runs
};

#endif // SCHEDULER_H
