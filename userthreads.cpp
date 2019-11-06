#include <iostream>
#include "userthreads.h"
#include <setjmp.h>
#include <list>
#include "queue"
#include "Thread.h"
#include <signal.h>
#include <sys/time.h>
#include <iostream>
#include "sleeping_threads_list.h"

//**** GLOBAL CONSTANTS AND DATA STRUCTS ****//
/*
 * holds pointers to all threads. ordered by id.
 */
static Thread *threads[MAX_THREAD_NUM] = {nullptr};
static std::list<Thread *> ready_threads;
static std::list<Thread *> blocked_threads;
static std::list<Thread *> for_deletion;
static SleepingThreadsList sleeping_threads;
static Thread *running;
static std::priority_queue<int, std::vector<int>, std::greater<int>> available_id;
static int num_threads = 0;
static int total_quantums = 0;
struct sigaction sa_virtual = {0};
struct sigaction sa_real = {0};
struct itimerval virtual_timer;
struct itimerval real_timer;
extern std::ostream cerr;
sigset_t signal_set;
const std::string LIB_ERR = "thread library error: ";
const std::string SYS_ERR = "system error: ";
const std::string SEG_ERR = "real timer setter - sigaction error\n";
const std::string LIB_NO_THREAD_FOUND =  "No thread found in the given id\n";
const std::string LIB_BLOCK_MAIN_THREAD = "Blocking main thread is illegal\n";
const std::string LIB_SLEEP_MAIN_THREAD = "Can not put thread 0 to sleep\n";
const std::string LIB_TERMINATE_MAIN_THREAD = "Terminate main thread is illegal\n";
const std::string SET_WAKEUP_REAL_FAILED = "the set of wakeup for real timer failed\n";
const std::string SET_WAKEUP_VIRTUAL_FAILED = "the set of wakeup for virtual timer failed\n";
const std::string SIGACTION_ERR ="couldn't assign the handler\n"; ///
const std::string SETITIMER_ERR = "coudn't the set the timer\n";
const std::string UNVALID_QUANTUM = "Unvalid quantum\n";
const std::string SIZE_ERR_SPAWN = " Can not spawn thread due to quantaty of threads\n";

void context_switch(State target);

void virtual_handler(int);

void restart_real_timer()
{
    if (setitimer(ITIMER_REAL, &real_timer, nullptr))
    {
        std::cerr << SYS_ERR << SET_WAKEUP_REAL_FAILED;
    }
}

void restart_virtual_timer()
{
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, nullptr))
    {
        std::cerr << SYS_ERR << SET_WAKEUP_VIRTUAL_FAILED;
    }
}

void wake_up(int)
{

    timeval now = {0};
    gettimeofday(&now, nullptr);
    while (!sleeping_threads.empty() && timercmp(&now, sleeping_threads.peek()->get_time_val(), >=) )
    {
        if (sleeping_threads.peek()->get_state() == READY)
        {
            sleeping_threads.peek()->set_state(READY);
            sleeping_threads.peek()->reset_time_val();
            sleeping_threads.peek()->set_awake();
            ready_threads.push_front(sleeping_threads.peek());
            sleeping_threads.pop();
        }
        else if (sleeping_threads.peek()->get_state() == BLOCKED)
        {
            sleeping_threads.peek()->set_state(BLOCKED);
            sleeping_threads.peek()->set_awake();
            sleeping_threads.peek()->reset_time_val();
            blocked_threads.push_front(sleeping_threads.peek());
            sleeping_threads.pop();
        }

        gettimeofday(&now, nullptr);
    }
    itimerval it = {0};
    if (!sleeping_threads.empty())
    {
        timersub(sleeping_threads.peek()->get_time_val(),&now, &it.it_value);
        real_timer.it_value.tv_sec = it.it_value.tv_sec;
        real_timer.it_value.tv_usec = it.it_value.tv_usec;

        // configure the real timer to run once:
        real_timer.it_interval = {0};
        restart_real_timer();
    }
    else
    {
        real_timer.it_value = {0};
        real_timer.it_interval = {0};
        restart_real_timer();
    }


}


/**
 * this function sets the real timer.
 */
void set_real_timer(int quantum)
{
    // Install timer_handler as the signal virtual_handler for SIGVTALRM.
    sa_real.sa_handler = &wake_up;
    if (sigaction(SIGALRM, &sa_real, nullptr) < 0)
    {
        std::cerr << SYS_ERR << SEG_ERR;
    }

    // Configure the virtual_timer to expire after 1 sec... /
    real_timer.it_value.tv_sec = quantum / 1000000;        // first time interval, seconds part
    real_timer.it_value.tv_usec = quantum % 1000000;        // first time interval, microseconds part

    // configure the real timer to reun once:
    real_timer.it_interval.tv_sec = 0;
    real_timer.it_interval.tv_usec = 0;

    // Start a virtual virtual_timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_REAL, &real_timer, nullptr))
    {
        std::cerr << SYS_ERR << SETITIMER_ERR;
    }
}


/**
 * this function sets the virtual timer.
 */
void set_virtual_timer(int quantum)
{
    // Install timer_handler as the signal virtual_handler for SIGVTALRM.
    sa_virtual.sa_handler = &virtual_handler;
    if (sigaction(SIGVTALRM, &sa_virtual, nullptr) < 0)
    {
        std::cerr << SYS_ERR << SIGACTION_ERR;
    }

    // Configure the virtual_timer to expire after 1 sec... /
    virtual_timer.it_value.tv_sec = quantum / 1000000;        // first time interval, seconds part
    virtual_timer.it_value.tv_usec = quantum % 1000000;        // first time interval, microseconds part

    // configure the virtual_timer to expire every 3 sec after that.
    virtual_timer.it_interval.tv_sec = quantum / 1000000;    // following time intervals, seconds part
    virtual_timer.it_interval.tv_usec = quantum % 1000000;    // following time intervals, microseconds part

    // Start a virtual virtual_timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, nullptr))
    {
        std::cerr << SYS_ERR << SETITIMER_ERR;
    }
}

/**
 * Function is used to create the id's of the threads
 */
void create_ids()
{
    for (int i = 0; i < MAX_THREAD_NUM; i++)
        available_id.push(i);
}

/**
 * Function is used to get the minimal id available.
 * @return the minimal id available
 */
int get_available_id()
{

    int id = available_id.top();
    available_id.pop();
    return id;
}

/**
 * Function is used to block the signals. This function will be used so during a certain
 * functions there will be no disturbances caused by signals.
 */
void block_time_signal()
{
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);
}

/**
 * Function is used to unblock the signals. This function will be used to end the
 * blocking of signals
 */
void unblock_time_signal()
{

    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
}

timeval calc_wake_up_timeval(int usecs_to_sleep)
{

    timeval now, time_to_sleep, wake_up_timeval;
    gettimeofday(&now, nullptr);
    time_to_sleep.tv_sec = usecs_to_sleep / 1000000;
    time_to_sleep.tv_usec = usecs_to_sleep % 1000000;
    timeradd(&now, &time_to_sleep, &wake_up_timeval);
    return wake_up_timeval;
}

void switch_thread_context(Thread* old,Thread *newt)
{
    int ret = sigsetjmp(*old->get_env(), 1);
    if (ret == 1)
    {
        return;
    }
    unblock_time_signal();
    siglongjmp(*newt->get_env(), 1);
}

/**
 * this method controls all context switche of the running  thread between all states.
 * @param id id of the thread to change. assumes id is legal (a thread with id exists.)
 * @param target the target state.
 */
void context_switch()
{
    Thread * old = running;
    running->set_state(READY);
    ready_threads.push_front(running);
    running = ready_threads.back();
    ready_threads.pop_back();
    running->increase_quantum();
    running->set_state(RUNNING);
    total_quantums++;

    switch_thread_context(old,running);

}

void virtual_handler(int)
{
    context_switch();
    restart_virtual_timer();
    unblock_time_signal();

}


void terminate_to_del()
{
    while (!for_deletion.empty())
    {
        delete for_deletion.front();
        for_deletion.pop_front();
    }
}


/*
Description: This function initializes the thread library. You may assume that this function is called before
any other thread library function, and that it is called exactly once. The input to the function is the length of
a quantum in micro-seconds. It is an error to call this function with non-positive quantum_usecs.
Return value: On success, return 0. On failure, return -1.*/
int uthread_init(int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        std::cerr << LIB_ERR << UNVALID_QUANTUM;
        return -1;
    }
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGVTALRM);
    sigaddset(&signal_set, SIGALRM);
    create_ids();
    uthread_spawn(nullptr); // create thread 0.
    running = ready_threads.back();
    ready_threads.pop_back();
    running->set_state(RUNNING);
    running->increase_quantum();
    total_quantums++;
    unblock_time_signal();
    set_virtual_timer(quantum_usecs);
    return 0;
}


/*
Description: This function creates a new thread, whose entry point is the function f with the signature
void f(void). The thread is added to the end of the READY threads list. The uthread_spawn function should
fail if it will cause the number of concurrent threads to exceed the limit (MAX_THREAD_NUM). Each
thread should be allocated with a stack of size STACK_SIZE bytes.
Return value: On success, return the ID of the created thread. On failure, return -1.*/
int uthread_spawn(void (*f)(void))
{
    block_time_signal();
    if (num_threads == MAX_THREAD_NUM)
    {        std::cerr << LIB_ERR << SIZE_ERR_SPAWN ;
        return -1;}

    auto id = get_available_id();
    auto *thread = new Thread(id, f);
    threads[id] = thread;
    ready_threads.push_front(thread);
    num_threads++;
    unblock_time_signal();
    return id;
}


/**
 * This function is resposnsible to terminate a thread out of a given input list lst
 * @param id The id of the thread to be terminated
 * @param lst The current state list which the thread is currently at.
 */
void terminate_out_of_list(int id, std::list<Thread *> lst)
{
    block_time_signal();
    lst.remove(threads[id]);
    delete threads[id];
    threads[id] = nullptr;
    available_id.push(id);
    num_threads--;
    unblock_time_signal();
}


/**
 * This function checks if there is any thread which it's id is as the given input
 * @param id The id to check with
 * @return 0 if there is no thread exist with the given id, 1 if a thread exist with the given id.
 */
int is_legal(int id)
{
    block_time_signal();
    if (id < 0 || id >= MAX_THREAD_NUM || threads[id] == nullptr)
    {
        return 0;
    }
    return 1;
}

/**
 * Function is used to free all the memory.
 */
void free_all_memory() // Check if there are more data structures to delete due to amitay changes
{
    block_time_signal();
    ready_threads.clear();
    blocked_threads.clear();
    while (!sleeping_threads.empty())
    {
        delete sleeping_threads.peek();
        sleeping_threads.pop();
    }
    delete running;
}

void update_timer(int tid)
{
    block_time_signal();
    if (threads[tid] == sleeping_threads.peek())
    {
        if (sleeping_threads.get_second() == nullptr)
        {
            real_timer.it_value = {0};
            restart_real_timer();
            return;
        }
        itimerval it = {0};
        timersub(sleeping_threads.get_second()->get_time_val(), sleeping_threads.peek()->get_time_val(), &it.it_value);
        timeradd(sleeping_threads.peek()->get_time_val(), &it.it_value, &real_timer.it_value);
        // configure the real timer to run once:
        real_timer.it_interval = {0};
        restart_real_timer();
    }
    unblock_time_signal();

}


/*
Description: This function terminates the thread with ID tid and deletes it from all relevant control
structures. All the resources allocated by the library for this thread should be released. If no thread with ID
tid exists it is considered an error. Terminating the main thread (tid == 0) will result in the termination of the
entire process using exit(0) (after releasing the assigned library memory).
Return value: The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread
terminates itself or the main thread is terminated, the function does not return.
 */
int uthread_terminate(int tid)
{
    block_time_signal();
    terminate_to_del();
    if (!is_legal(tid))
    {
        std::cerr << LIB_ERR << LIB_NO_THREAD_FOUND;
        unblock_time_signal();
        return -1;
    }
    if (tid == 0)
    {
        free_all_memory(); // Release assigned assigned memory
        std::cerr << LIB_ERR << LIB_TERMINATE_MAIN_THREAD;
        unblock_time_signal();
        exit(EXIT_FAILURE);
        //todo check exit-failure.
    }
    if (threads[tid]->is_sleep())
    {
        update_timer(tid);
        for_deletion.push_front(threads[tid]);
        sleeping_threads.remove_thread(tid);
        threads[tid] = nullptr;
        threads[tid] = nullptr;
        available_id.push(tid);
        num_threads--;
        unblock_time_signal();
    } else
    {
        switch (threads[tid]->get_state())
        {
            case RUNNING:
            {
                Thread * old = running;
                for_deletion.push_front(old);
                running = ready_threads.back();
                running->set_state(RUNNING);
                ready_threads.pop_back();
                total_quantums++;
                running->increase_quantum();
                switch_thread_context(old,running);
                unblock_time_signal();
                return 0;
            }
            case READY:
            {
                block_time_signal();
                ready_threads.remove(threads[tid]);
                delete threads[tid];
                threads[tid] = nullptr;
                available_id.push(tid);
                num_threads--;
                unblock_time_signal();
                return 0;
            }
            case BLOCKED:
            {
                block_time_signal();
                blocked_threads.remove(threads[tid]);
                delete threads[tid];
                threads[tid] = nullptr;
                available_id.push(tid);
                num_threads--;
                unblock_time_signal();
                return 0;
            }
            default:
                return 0;
        }
    }
    return 0;
}


/*
Description: This function blocks the thread with ID tid. The thread may be resumed later using
uthread_resume. If no thread with ID tid exists it is considered an error. In addition, it is an error to try
blocking the main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made.
Blocking a thread in BLOCKED state has no effect and is not considered an error.
Return value: On success, return 0. On failure, return -1. */
int uthread_block(int tid)
{
    block_time_signal();

    terminate_to_del();
    if (!is_legal(tid))
    {
        std::cerr << LIB_ERR << LIB_NO_THREAD_FOUND;
        unblock_time_signal();
        return -1;
    }
    if (tid == 0)
    {
        std::cerr << LIB_ERR << LIB_BLOCK_MAIN_THREAD;
        unblock_time_signal();
        return -1;
    }
    if (threads[tid]->is_sleep())
    {

        threads[tid]->set_state(BLOCKED);
        unblock_time_signal();

    } else
    {
        switch (threads[tid]->get_state())
        {
            case RUNNING:
            {
                Thread * old = running;
                old->set_state(BLOCKED);
                blocked_threads.push_front(old);
                running = ready_threads.back();
                running->set_state(RUNNING);

                ready_threads.pop_back();
                total_quantums++;
                running->increase_quantum();
                switch_thread_context(old,running);
                unblock_time_signal();
                break;
            }
            case READY:
            {
                ready_threads.remove(threads[tid]);
                threads[tid]->set_state(BLOCKED);
                blocked_threads.push_front(threads[tid]);
                unblock_time_signal();
                break;
            }
            case BLOCKED:
            {
                unblock_time_signal();
                break;
            }
            default:
                return 0;
        }

    }
    return 0;
}

/*
Description: This function resumes a blocked thread with ID tid and moves it to the READY state.
Resuming a thread in a RUNNING or READY state has no effect and is not considered an error. If no
thread with ID tid exists it is considered an error.
Return value: On success, return 0. On failure, return -1. */
int uthread_resume(int tid)
{

    block_time_signal();
    terminate_to_del();
    if (!is_legal(tid))
    {
        std::cerr << LIB_ERR << LIB_NO_THREAD_FOUND;
        unblock_time_signal();
        return -1;
    }
    if (threads[tid]->is_sleep())
    {
        threads[tid]->set_state(READY);
        unblock_time_signal();
    } else
    {
        switch (threads[tid]->get_state())
        {
            case RUNNING: // No affect
            {
                unblock_time_signal();
                break;
            }
            case READY: // No affect
            {
                unblock_time_signal();
                break;
            }
            case BLOCKED:
            {
                blocked_threads.remove(threads[tid]);
                threads[tid]->set_state(READY);
                ready_threads.push_front(threads[tid]);
                unblock_time_signal();
                break;
            }
            default:
                break;
        }
    }
    return 0;
}

unsigned int calc_real_wake_up(unsigned int usec)
{
    block_time_signal();
    if (sleeping_threads.empty())
    {
        return usec;
    }
    timeval tv = calc_wake_up_timeval(usec);
    running->set_time_val(tv);
    if (timercmp(&tv,sleeping_threads.peek()->get_time_val(),>=))
    {
        return 0;
    } else
    {
        timeval tv = {0};
        timersub(sleeping_threads.peek()->get_time_val(),running->get_time_val(),&tv);
        return tv.tv_usec + tv.tv_sec*1000000;
    }

}

int uthread_sleep(unsigned int usec)
{
    block_time_signal();
    terminate_to_del();
    if (running->get_id() == 0)
    {
        std::cerr << LIB_ERR << LIB_SLEEP_MAIN_THREAD;
        unblock_time_signal();
        return -1;
    }
    if (usec <= 0)
    {
        Thread * old = running;
        old->set_state(READY);
        ready_threads.push_front(old);
        running = ready_threads.back();
        running->set_state(RUNNING);
        ready_threads.pop_back();
        total_quantums++;
        running->increase_quantum();
        switch_thread_context(old,running);
    } else
    {
        unsigned int res  = calc_real_wake_up(usec);
        if (res > 0)
        {
            set_real_timer(res);
        }

        Thread * old = running;
        old->set_state(READY);
        old->set_sleep();
        sleeping_threads.add(old);
        running = ready_threads.back();
        running->set_state(RUNNING);
        ready_threads.pop_back();
        total_quantums++;
        running->increase_quantum();
        switch_thread_context(old,running);
    }
    return 0;
}

int uthread_get_tid()
{
    return running->get_id();
}


int uthread_get_total_quantums()
{
    return total_quantums;
}

int uthread_get_quantums(int tid)
{
    block_time_signal();
    if (!is_legal(tid))
    {
        std::cerr << LIB_ERR << LIB_NO_THREAD_FOUND;
        unblock_time_signal();
        return -1;
    }
    unblock_time_signal();
    return threads[tid]->get_quantum();

}

