#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/*******************************************************************/
//List to hold sleeping threads, sorted so the least thread with wake up ticks is on the beining of the list.
struct list sleeping_threads_list;
//lock to synchronize editing the sleeping_threads_list
struct lock lock_sleeping_threads_list;
/*******************************************************************/

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void)
{
    pit_configure_channel (0, 2, TIMER_FREQ);
    intr_register_ext (0x20, timer_interrupt, "8254 Timer");
    /*******************************************************************/
    list_init (&sleeping_threads_list);
    lock_init (&lock_sleeping_threads_list);
    /*******************************************************************/
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void)
{
    unsigned high_bit, test_bit;

    ASSERT (intr_get_level () == INTR_ON);
    printf ("Calibrating timer...  ");

    /* Approximate loops_per_tick as the largest power-of-two
       still less than one timer tick. */
    loops_per_tick = 1u << 10;
    while (!too_many_loops (loops_per_tick << 1))
    {
        loops_per_tick <<= 1;
        ASSERT (loops_per_tick != 0);
    }

    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops (loops_per_tick | test_bit))
            loops_per_tick |= test_bit;

    printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void)
{
    enum intr_level old_level = intr_disable ();
    int64_t t = ticks;
    intr_set_level (old_level);
    return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then)
{
    return timer_ticks () - then;
}
/*******************************************************************/
//Comparator to be passed for list_insert_ordered to sort threads by their wake up ticks
bool
less_thread_wake_up_ticks_comp (const struct list_elem* a, const struct list_elem* b, void* aux UNUSED)
{
    int64_t a_wake_up_ticks = (list_entry(a, struct thread, sleeping_elem))->wake_up_ticks;
    int64_t b_wake_up_ticks = (list_entry(b, struct thread, sleeping_elem))->wake_up_ticks;
    return a_wake_up_ticks < b_wake_up_ticks;
}
/*******************************************************************/

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
    int64_t start = timer_ticks ();

    ASSERT (intr_get_level () == INTR_ON);
    /*******************************************************************/
    /* Make sure that ticks to sleep isn't either zero or negative (instant wake up).
       acquire the lock_sleeping_threads_list avoide many threads overlap reading/writing the list.
       Set the thread wake up ticks and insert it in its decreasing order to the sleaping threads list.
       Disable the interrupt to block the thread to put it into sleep.
       (Note we are inserting the threads in order to avoide much overhead at interrupt handlers when waking them up,
       so the interrupt handler will check if its time to wake the first thread in the list, if so wake the thread up and,
       check the next thread, else do nothing)
     */
    if (ticks > 0)
    {

        lock_acquire (&lock_sleeping_threads_list);

        struct thread* thread_to_sleep = thread_current ();
        thread_to_sleep->wake_up_ticks = timer_ticks () + ticks;
        list_insert_ordered (&sleeping_threads_list, &(thread_to_sleep->sleeping_elem), &less_thread_wake_up_ticks_comp, NULL);

        lock_release (&lock_sleeping_threads_list);

        enum intr_level old_level;
        ASSERT (!intr_context ());
        old_level = intr_disable ();

        thread_block ();

        intr_set_level (old_level);
    }
    /*******************************************************************/
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms)
{
    real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us)
{
    real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns)
{
    real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms)
{
    real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us)
{
    real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns)
{
    real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void)
{
    printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
    ticks++;
    thread_tick ();

    /*******************************************************************/
    /* Disable the interrupts so an interrupt can't be interrupted (maybe in the future).
       Check if the sleeping threads list is being edited (a thread is holding the lock), return
       Iterate the sorted sleeping threads list, if it's time to wake up the first thread (least wake up ticks)
       Unblock the thread, remove it from the list, and check the next (first) thread, else break
       (Note we are enabling the interrupts at safe zones and disabling it over and over to avoid performance issues) */
    enum intr_level old_level;
    old_level = intr_disable ();

    if (lock_sleeping_threads_list.holder != NULL)
        return;

    for(struct list_elem* iter = list_begin(&sleeping_threads_list); iter != list_end(&sleeping_threads_list); iter = list_remove(iter))
    {
        struct thread* sleeping_thread = list_entry(iter, struct thread, sleeping_elem);
        if (timer_ticks() >= sleeping_thread->wake_up_ticks)
        {
            thread_unblock (sleeping_thread);
            intr_set_level (old_level);
        }
        else
        {
            intr_set_level (old_level);
            break;
        }

        old_level = intr_disable ();
    }
    /*******************************************************************/
    if (thread_mlfqs)
    {
        //Each tick must increment recent_cpu
        increment_recent_cpu();

        //Each second must do two tasks:
        //update recent_cpu for all threads
        //update load_avg of the system

        if (ticks % TIMER_FREQ == 0){
            thread_foreach(update_recent_cpu, NULL);
            update_load_avg();
        }//Each 4 ticks -> each thread's priority is recalculated
        else if (ticks % 4 == 0){
            thread_foreach(update_priority, NULL);
            //After updating priorities should resort the list of ready threads
            //And after resorting ready list, thread yield should be called
            resort_ready_threads();
        }

    }
    /*******************************************************************/
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops)
{
    /* Wait for a timer tick. */
    int64_t start = ticks;
    while (ticks == start)
        barrier ();

    /* Run LOOPS loops. */
    start = ticks;
    busy_wait (loops);

    /* If the tick count changed, we iterated too long. */
    barrier ();
    return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops)
{
while (loops-- > 0)
barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom)
{
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.

          (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
       1 s / TIMER_FREQ ticks
    */
    int64_t ticks = num * TIMER_FREQ / denom;

    ASSERT (intr_get_level () == INTR_ON);
    if (ticks > 0)
    {
        /* We're waiting for at least one full timer tick.  Use
           timer_sleep() because it will yield the CPU to other
           processes. */
        timer_sleep (ticks);
    }
    else
    {
        /* Otherwise, use a busy-wait loop for more accurate
           sub-tick timing. */
        real_time_delay (num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
    /* Scale the numerator and denominator down by 1000 to avoid
       the possibility of overflow. */
    ASSERT (denom % 1000 == 0);
    busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}