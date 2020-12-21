// scheduler.cc
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would
//	end up calling FindNextToRun(), and that would put us in an
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler() {
    L1List = new SortedList<Thread *>(CompareSJF); // L1 queue, preemptive
    L2List =
        new SortedList<Thread *>(ComparePriority); // L2 queue, non-preemptive
    L3List = new SortedList<Thread *>(CompareRR);  // L3 queue
    schedulerType = Priority;
    toBeDestroyed = NULL;
}
int Scheduler::CompareRR(Thread *x, Thread *y) { return 1; }
int Scheduler::CompareSJF(Thread *x, Thread *y) {
    return x->getBurstTime() - y->getBurstTime();
}
int Scheduler::ComparePriority(Thread *x, Thread *y) {
    return y->getPriority() - x->getPriority();
}
//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler() {
    delete L1List;
    delete L2List;
    delete L3List;
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void Scheduler::ReadyToRun(Thread *thread) {
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
    // cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    size_t label;
    if (thread->getPriority() > 99) {
        label = 1;
        L1List->Insert(thread);
    } else if (thread->getPriority() > 49) {
        label = 2;
        L2List->Insert(thread);
    } else {
        label = 3;
        L3List->Insert(thread);
    }
    // thread->resetWaitingAge();
    DEBUG(dbgSche, "Tick [ " << kernel->stats->totalTicks << " ]: Thread [ " << thread->getID() << " ] "<< "is inserted into queue L[ " << label << " ]");
}
int Scheduler::getQueueLabel() {
    if (schedulerType == SJF)
        return 1;
    else if (schedulerType == Priority)
        return 2;
    else
        return 3;
}
bool Scheduler::isPreemptive() {
    if ((schedulerType == Priority) or (schedulerType == SJF && L1List->IsEmpty()))
        return false;
    return true;
}
void Scheduler::ageUpdate() {
    SortedList<Thread *> *nL1List = new SortedList<Thread *>(CompareSJF);      // new L1 queue, preemptive
    SortedList<Thread *> *nL2List = new SortedList<Thread *>(ComparePriority); // new L2 queue, non-preemptive
    SortedList<Thread *> *nL3List = new SortedList<Thread *>(CompareRR);       // new L3 queue
    Thread *t;
    size_t oldPriority;
    while (!L1List->IsEmpty()) {
        t = L1List->RemoveFront();
        if (t->increaseAge()){
            oldPriority = t->getPriority();
            t->setPriority(oldPriority + 10);
            DEBUG(dbgSche, "Tick [ " << kernel->stats->totalTicks << " ]: Thread [ " << t->getID() << " ] "
                                    << "changes its priority from [" << oldPriority << "] to [" << t->getPriority() << "]");
        }
        nL1List->Insert(t);
    }
    while (!L2List->IsEmpty()) {
        t = L2List->RemoveFront();
        if (t->increaseAge()) {
            oldPriority = t->getPriority();
            t->setPriority(oldPriority + 10);
            DEBUG(dbgSche, "Tick [ " << kernel->stats->totalTicks << " ]: Thread [ " << t->getID() << " ] "
                                    << "changes its priority from [" << oldPriority << "] to [" << t->getPriority() << "]");
        }
        if (t->getPriority() > 99) {
            nL1List->Insert(t);
        } else {
            nL2List->Insert(t);
        }
    }
    while (!L3List->IsEmpty()) {
        t = L3List->RemoveFront();
        if (t->increaseAge()) {
            oldPriority = t->getPriority();
            t->setPriority(oldPriority + 10);
            DEBUG(dbgSche, "Tick [ " << kernel->stats->totalTicks << " ]: Thread [ " << t->getID() << " ] "
                                    << "changes its priority from [" << oldPriority << "] to [" << t->getPriority() << "]");
        }
        if (t->getPriority() > 49) {
            nL2List->Insert(t);
        } else {
            nL3List->Insert(t);
        }
    }
    L1List = nL1List;
    L2List = nL2List;
    L3List = nL3List;
}
//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *Scheduler::FindNextToRun() {
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (!L1List->IsEmpty()) {
        schedulerType = SJF;
        return L1List->RemoveFront();
    } else if (!L2List->IsEmpty()) {
        schedulerType = Priority;
        return L2List->RemoveFront();
    } else if (!L3List->IsEmpty()) {
        schedulerType = RR;
        return L3List->RemoveFront();
    } else {
        return NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void Scheduler::Run(Thread *nextThread, bool finishing) {
    Thread *oldThread = kernel->currentThread;

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) { // mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL) { // if this thread is a user program,
        oldThread->SaveUserState(); // save the user's CPU registers
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow(); // check if the old thread
                                // had an undetected stack overflow

    kernel->currentThread = nextThread; // switch to the next thread
    nextThread->setStatus(RUNNING);     // nextThread is now running
    nextThread->setStartTime(kernel->stats->totalTicks);
    DEBUG(dbgThread, "Switching from: " << oldThread->getName()
                                        << " to: " << nextThread->getName());
    DEBUG(dbgSche, "Tick [ " << kernel->stats->totalTicks << " ]: Thread [ " << nextThread->getID() << " ] is now selected for execution, thread [ " << oldThread->getID() << " ] is replaced, and it has executed [ " << oldThread->getAccumulatedTime() << " ] ticks.");

    // This is a machine-dependent assembly language routine defined
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread

    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed(); // check if thread we were running
                          // before this one has finished
                          // and needs to be cleaned up

    if (oldThread->space != NULL) {    // if there is an address space
        oldThread->RestoreUserState(); // to restore, do it.
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void Scheduler::CheckToBeDestroyed() {
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void Scheduler::Print() {
    cout << "Ready list contents:\n";
    L1List->Apply(ThreadPrint);
    L2List->Apply(ThreadPrint);
    L3List->Apply(ThreadPrint);
}
