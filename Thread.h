//
// Created by amitai on 4/8/19.
//
#include <setjmp.h>
#include <bits/types/struct_timeval.h>

#ifndef OS2_THREAD_H
#define OS2_THREAD_H

typedef enum STATES
{
    RUNNING, READY, BLOCKED, TERMINATE
} State;

class Thread
{
public:
    Thread(int id, void(*f)(void));

    ~Thread();

    /**
     * gets thread id
     * @return
     */
    int get_id(); //id is set once during creation.
    /**
     * gets thread state
     * @return
     */
    State get_state();

    /**
     * sets thread state
     */
    void set_state(State);

    /**
     * gets the current environment variables.
     * @return
     */
    sigjmp_buf *get_env();

    /*
     * returns the currents quantum.
     */
    int get_quantum();

    /**
     * gets the time variable if available. otherwise nullptr
     * @return
     */
    timeval *get_time_val();

    void set_time_val(timeval tv);

    void set_sleep();
    void set_awake();
    bool operator==(const Thread &b) const;
    void increase_quantum();
    void reset_time_val();
    bool is_sleep();
private:
    char *stack;
    int tid;
    State state;
    int quantums;
    bool sleep;
    sigjmp_buf current_env;
    timeval awaken_tv;


};


#endif //OS2_THREAD_H
