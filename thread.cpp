#include "thread.h"
#include <iostream>


/**
 * A constructor that creates the main thread.
 */
Thread::Thread():_id(0), _state(RUNNING), _num_of_quantum(0),
_quantums_left_in_sleeping(0), _env() {}

/**
 * A constructor that creates the threads other than the main thread.
 * @param id The id of the thread.
 * @param entry_point The entry point of the thread.
 */
Thread::Thread(unsigned int id, thread_entry_point entry_point): _id(id),
_state(READY), _thread_entry_point(entry_point), _num_of_quantum(0),
_quantums_left_in_sleeping(0), _env(){
    /*_stack = new char[STACK_SIZE];
    // _env = &buf;
    // Calculating sp and pc
    address_t sp = (address_t) _stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) _thread_entry_point;
    // Saving the thread
    sigsetjmp(_env, 1);
    (_env->__jmpbuf)[JB_SP] = translate_address(sp);
    (_env->__jmpbuf)[JB_PC] = translate_address(pc);*/
}

unsigned int Thread::get_id() const{
    return _id;
}

State Thread::get_state() const{
    return _state;
}

char *Thread::get_stack() const{
    return _stack;
}

int Thread::get_quantum_num() const{
    return _num_of_quantum;
}

int Thread::get_quantums_left() const{
    return _quantums_left_in_sleeping;
}

void Thread::set_state(State state){
    _state = state;
}

void Thread::set_stack(char *stack){
    _stack = stack;
}

void Thread::set_id(unsigned int id) {
    _id = id;
}

void Thread::free_stack() {
    delete[] _stack;
}

void Thread::increase_quantum() {
    _num_of_quantum++;
}

void Thread::decrease_quantums_left(){
    _quantums_left_in_sleeping--;
}

void Thread::set_sleeping_quantum(int num_quantums){
    _quantums_left_in_sleeping = num_quantums;
}
