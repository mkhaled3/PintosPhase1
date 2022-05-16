/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    /*******************************************************************/
    int *priority;                      /* pointer for the waiting thread priority, used to unblock the highest priority thread on condvars */
    /*******************************************************************/
  };

void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  old_level = intr_disable ();
  /*******************************************************************/
  //select the maximum priority thread waiting on the semaphore in order for it to be unblocked first
  bool reschedule = false;
  if (!list_empty (&sema->waiters)) 
    {
      struct list_elem *max_priority_elem = list_max ((&sema->waiters), &less_thread_priority_comp, NULL);
      struct thread *max_priority_thread = list_entry (max_priority_elem, struct thread, elem);
      list_remove (max_priority_elem);
      if (max_priority_thread->priority > thread_current()->priority)
        reschedule = true;
      thread_unblock (max_priority_thread);
    }
  /*******************************************************************/
  sema->value++;
  intr_set_level (old_level);
  /*******************************************************************/
  //check if the waked thread priority is higher than the currently running one, reschedule.
  if (reschedule)
    thread_yield();
  /*******************************************************************/
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
       (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  /*******************************************************************/
  lock->priority = 0;
  /*******************************************************************/
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  /*******************************************************************/
  /* if the thread failed to acquire the lock, set its waiting_on_lock.
     Disable the interrupts to avoide many threads overlabing editing/reading the lock global info.
     if its priority is higher than the lock's priority, set the lock's and its holder's priority to the thread priority and donate it for the lock holder.
     block the thread, after it's unblocked set its waiting_on_lock to null and insert the lock to its aquired_locks in order by the lock's priority,
     in order to be sellected later in priority donation */
  if (thread_mlfqs)
  {
    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
    return;
  }
  
  if (!lock_try_acquire(lock)){
    thread_current()->waiting_on_lock = lock;

    enum intr_level old_level;
    ASSERT (!intr_context ());
    old_level = intr_disable ();

    if (lock->priority < thread_current()->priority){
      lock->priority = thread_current()->priority;
      lock->holder->priority = lock->priority;
      nested_donation(lock->holder);
    }

    intr_set_level (old_level);
    
    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
    
    thread_current()->waiting_on_lock = NULL;
    list_push_back (&(lock->holder->aquired_locks), &(lock->myLock));
  }
  /*******************************************************************/
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success){
    lock->holder = thread_current ();
    /*******************************************************************/
    if (!thread_mlfqs)
    {
      /* if the thread acquired the lock check if the threade will donate its priority to the lock or vice versa
        insert the lock to its aquired_locks in order to be sellected later in priority donation */
      if (lock->priority > lock->holder->priority)
        lock->holder->priority = lock->priority;
      else
        lock->priority = lock->holder->priority;
      list_push_back (&(lock->holder->aquired_locks), &(lock->myLock));
    }
    /*******************************************************************/
  }

  return success;
}

/* Releases LOCK, which must be owned by the currطيب بصent thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  /*******************************************************************/
  if (!thread_mlfqs){
    //remove the lock from the thread's acquired lock list
    list_remove(&(lock->myLock));

    /* if there is threads waiting on the lock set the lock priority to the maximum thread's priority waiting on its semaphore, else set its priority to 0 */
    if (!list_empty(&(lock->semaphore.waiters))){
      struct list_elem *max_priority_elem = list_max ((&lock->semaphore.waiters), &less_thread_priority_comp, NULL);
      struct thread *max_priority_thread = list_entry (max_priority_elem, struct thread, elem);
      lock->priority = max_priority_thread->priority;
    }else{
      lock->priority = 0;
    }

    /* if the thread is holding another locks sellect the highest lock's priority,
      then sellect the highest priority either of the lock or of the thread original priority, and assign it to each others,
      else retrieve the thread original priority */
    if (!list_empty(&(lock->holder->aquired_locks))){
      struct list_elem *max_priority_elem = list_max (&(lock->holder->aquired_locks), &less_lock_priority_comp, NULL);
      struct lock *max_priority_lock = list_entry (max_priority_elem, struct lock, myLock);
      if (max_priority_lock->priority > lock->holder->real_priority)
        lock->holder->priority = max_priority_lock->priority;
      else
        lock->holder->priority = lock->holder->real_priority;      
    }else{
      lock->holder->priority = lock->holder->real_priority;
    }
    /*******************************************************************/
  }
  
  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  /*******************************************************************/
  //set the semaphore priority to point for the waiter priority in order to select max priority thread to signal it using list_max at cond_signal
  waiter.priority = &(thread_current()->priority);
  /*******************************************************************/
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&(waiter.semaphore));
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  /*******************************************************************/
  //find the maximum priority semaphore (waiting thread) to be signaled first
  if (!list_empty(&(cond->waiters))){
    struct list_elem *max_priority_elem = list_max ((&cond->waiters), &less_sema_priority_comp, NULL);
    struct semaphore_elem *max_priority_sema = list_entry (max_priority_elem, struct semaphore_elem, elem);
    list_remove (max_priority_elem);
    sema_up (&max_priority_sema->semaphore);
  }
  /*******************************************************************/
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/*******************************************************************/
//Comparator to be passed for list_max to get the lock with maximum priority to be used in priority donation
bool
less_lock_priority_comp(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED){
  int a_priority = (list_entry(a, struct lock, myLock))->priority;
  int b_priority = (list_entry(b, struct lock, myLock))->priority;
  return a_priority < b_priority;
}

//Comparator to be passed for list_max to find maximum thread's waiting semaphore priority in condvars
bool
less_sema_priority_comp(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED){
  int *a_prioirty = (list_entry(a, struct semaphore_elem, elem)->priority);
  int *b_priority = (list_entry(b, struct semaphore_elem, elem)->priority);
  return *a_prioirty < *b_priority;
}
/*******************************************************************/


