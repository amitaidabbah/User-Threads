//
// Created by amitai on 4/8/19.
//
#include "uthreads.h"
#include "Thread.h"
#include <time.h>
#include <signal.h>

/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

Thread::Thread(int id, void(*f)(void)) : tid(id), state(READY), quantums(0),sleep(false)
{
    stack = new char[STACK_SIZE];
    address_t sp, pc;

    sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) f;
    sigsetjmp(current_env, 1);
    (current_env->__jmpbuf)[JB_SP] = translate_address(sp);
    (current_env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&current_env->__saved_mask);

}

Thread::~Thread()
{
    delete stack;

}

int Thread::get_id()
{
    return tid;
}

State Thread::get_state()
{
    return state;
}

void Thread::set_state(State s)
{
    state = s;
}

sigjmp_buf *Thread::get_env()
{
    return &current_env;
}

int Thread::get_quantum()
{
    return quantums;
}

timeval *Thread::get_time_val()
{
    {
        return &awaken_tv;
    }
}

bool Thread::operator==(const Thread &b) const
{
    return (this == &b);
}


void Thread::set_time_val(timeval tv)
{
    awaken_tv = tv;
}

void Thread::reset_time_val()
{
    awaken_tv.tv_sec = 0;
    awaken_tv.tv_usec = 0;
}

bool Thread::is_sleep() {
    return sleep;
}

void Thread::set_sleep()
{
    sleep = true;
}

void Thread::set_awake()
{
    sleep = false;
}

void Thread::increase_quantum()
{
    quantums++;
}

