#ifndef EX2_THREAD_H
#define EX2_THREAD_H

#include "uthreads.h"
#include <setjmp.h>

/**
 * Enum to represent the current state of a thread.
 */
enum State { READY, RUNNING, BLOCKED, SLEEPING, SLEEPING_AND_BLOCKED, TERMINATED};

typedef unsigned long address_t;

/**
 * This class represents a single thread.
 */
class Thread {

private:
    // The id of the thread
    unsigned int _id;
    // Holds the current state of the thread
    State _state;
    thread_entry_point _thread_entry_point;
    char *_stack;
    // Holds the number of quantum the thread ran
    int _num_of_quantum;
    // Quantums left
    int _quantums_left_in_sleeping;
    // The jump buffer, holds the information of the thread environment
    sigjmp_buf buf;
public:
    /**
     * Constructor for main thread
     */
    sigjmp_buf _env;
    Thread();

    /**
     * A constructor for the threads other than the main thread.
     * @param id The id of the thread.
     * @param entry_point The entry point.
     */
    //TODO: Initialize _sleeping_time field.
    Thread(unsigned int id, thread_entry_point entry_point);

    /**
     * Getter for thread id
     * @return unsigned int
     */
    unsigned int get_id() const;

    /**
     * Getter of thread state
     * @return State
     */
    State get_state() const;

    /**
     * Getter of thread state
     * @return stack
     */

    char *get_stack() const;

    /**
     * Getter of the number of quantum the thread ran.
     * @return The number of quantum.
     */
    int get_quantum_num() const;

    /**
     * Getter for the number of quantums the thread needs to be in SLEEPING mode.
     * @return The number of quantums left to be asleep.
     */
    int get_quantums_left() const;

    /**
     * Setter for the state of the thread.
     * @param state The new state.
     */
    void set_state(State state);

    /**
     * Setter for the stack of the thread.
     * @param stack The new stack.
     */
    void set_stack(char *stack);

    /**
     * Setter for the id of the thread.
     * @param id The id of the thread.
     */
    void set_id(unsigned int id);

    /**
     * This function frees the stack of the thread.
     */
    void free_stack();

    /**
     * This function increases the number of quantum of a thread by one.
     */
    void increase_quantum();

    /**
    * This function increases the number of quantum of a thread by one.
    */
    void decrease_quantums_left();

    /**
     * Setter for the number of quantums a thread needs to sleep.
     * @param num_quantums The number of quantums.
     */
    void set_sleeping_quantum(int num_quantums);
};

#endif //EX2_THREAD_H
