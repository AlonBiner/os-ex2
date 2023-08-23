#include "thread.h"
#include <iostream>
#include <deque>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <set>
#include <unordered_map>

#ifdef __x86_64__
/* code for 64-bit Intel arch */

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

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif


#define MAIN_THREAD_TID 0

/* Error message templates */
#define THREAD_LIB_ERR "thread library error: "
#define SYSTEM_ERROR "system error: "

/* Error message texts */
#define INVALID_QUANTUM_TIME_ERR "quantum time invalid"
#define INVALID_MIN_TID_ERR "Minimal available thread exceed maximum number of threads"
#define INVALID_TID "There is no thread with this id"
#define INVALID_MAIN_BLOCK "Trying to block the main thread"
#define INVALID_MAIN_SLEEP "Trying to temporarily block the main thread"
#define SIGACTION_ERR "Sigaction error"
#define SET_TIMER_ERR "Set timer error"
#define SIG_SET_ERROR "Failed to create signals set"

unsigned int min_tid = 0;
struct sigaction sa = {};
struct itimerval timer;
int quantum_counter = 0;
sigset_t signal_set;

std::deque<Thread *> ready_threads;
std::deque<Thread *> blocked_threads;
std::deque<Thread *> sleeping_threads;

Thread *current_running_thread;
std::set<int> available_tids;
std::unordered_map<int, Thread *> tid_to_thread_map;

void nullify_timer() {
    // Set the timer
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr))
    {
        std::cerr << SYSTEM_ERROR << SET_TIMER_ERR << std::endl;
        exit(1);
    }
}

/**
 * This function initializes the timer.
 * @param quantum_usecs The length of a quantum in micro-seconds
 */
void initialize_timer(int quantum_usecs) {
    // Configure the timer to expire after 1 quantum
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;

    // configure the timer to expire every 1 quantum after that.
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    // Set the timer
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr))
    {
        std::cerr << SYSTEM_ERROR << SET_TIMER_ERR << std::endl;
        exit(1);
    }
}

void run_next_ready_thread() {
    current_running_thread = ready_threads.front();
    current_running_thread->set_state(RUNNING);
    current_running_thread->increase_quantum();
    quantum_counter++;
    ready_threads.pop_front();
    nullify_timer();
    // Jump to the next thread
    siglongjmp(current_running_thread->_env, 1);
}

void update_sleeping_time() {
    // Updating the remaining time of the sleeping threads
    for(auto thread : sleeping_threads){
        thread->decrease_quantums_left();
        // If the thread finishes its sleeping time
        if (thread->get_quantums_left() == 0){
            // If the thread is in sleeping state, hence, not blocked
            if(thread->get_state() == SLEEPING){
                // Move the thread to ready
                thread->set_state(READY);
                ready_threads.push_back(thread);
            }
            // If the thread is both sleeping and blocked
            else{
                // Set the state to blocked
                thread->set_state(BLOCKED);
            }
            // Remove the thread from the sleeping deque
            auto it = std::find(sleeping_threads.begin(), sleeping_threads.end(), thread);
            if(it != sleeping_threads.end()){
                sleeping_threads.erase(it);
            }
        }
    }
}

/**
 *  This function is the timer handler for the timer.
 * @param sig The timer signal.
 */
void timer_handler(int sig)
{
    int ret_val = -1;
    if (current_running_thread->get_state() == TERMINATED) {
        ret_val = 0;
    } else if (current_running_thread->get_state() == BLOCKED ||
            current_running_thread->get_state() == SLEEPING) {
        ret_val = sigsetjmp(current_running_thread->_env, 1);
    } else if(current_running_thread->get_state() == RUNNING){
        // sigjmp_buf env1 = *current_running_thread->get_env();
        ret_val = sigsetjmp(current_running_thread->_env, 1);
    }

    bool did_just_save_bookmark = ret_val == 0;

    if (did_just_save_bookmark) {
        // Updates the sleeping time of all the sleeping threads
        update_sleeping_time();
        if(current_running_thread->get_state() == RUNNING){
            // Set the current running thread to ready and save the environment
            current_running_thread->set_state(READY);
            ready_threads.push_back(current_running_thread);
        } else if (current_running_thread->get_state() == TERMINATED) {
            current_running_thread->free_stack();
            delete current_running_thread;
            current_running_thread = nullptr;
        }
        // Set the next thread in the ready deque to be the running thread
        run_next_ready_thread();
    }
}

/**
 * This function sets the fields of the timer and creates it.
 * @param quantum_usecs The length of a quantum in micro-seconds
 */
void create_timer(int quantum_usecs) {
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << SYSTEM_ERROR << SIGACTION_ERR << std::endl;
        exit(1);
    }
    initialize_timer(quantum_usecs);
}

/**
 * This function creates the signals set.
 */
void create_signal_set() {
    if(sigemptyset(&signal_set) < 0 ||
        sigaddset(&signal_set, SIGALRM) < 0){
        std::cerr << SYSTEM_ERROR << SIG_SET_ERROR << std::endl;
        exit(1);
    }
}

int uthread_init(int quantum_usecs) {
    // If the given quantum usecs number is not valid
    if (quantum_usecs <= 0) {
        std::cerr << THREAD_LIB_ERR << INVALID_QUANTUM_TIME_ERR << std::endl;
        return -1;
    }
    // Creating and setting the fields for the main thread
    current_running_thread = new Thread();
    current_running_thread->set_state(RUNNING);
    current_running_thread->increase_quantum();
    quantum_counter++;
    tid_to_thread_map[0] = current_running_thread;

    // Initializing a set that will hold all the available tid's
    for (int i = 1; i <= MAX_THREAD_NUM; ++i) {
        available_tids.insert(i);
    }
    min_tid = *available_tids.begin();
    // Constructing the signal set
    create_signal_set();
    // Creating the timer
    create_timer(quantum_usecs);

    return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);
    // If we can't create new threads

    if (min_tid >= MAX_THREAD_NUM) {
        std::cerr << THREAD_LIB_ERR << INVALID_MIN_TID_ERR << std::endl;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return -1;
    }
    // // Creating and setting the fields for a new thread
    Thread *thread_to_add = new Thread(min_tid, entry_point);
    ready_threads.push_back(thread_to_add);
    thread_to_add->set_state(READY);
    tid_to_thread_map[(int) min_tid] = thread_to_add;
    int used_id = (int) min_tid;
    // Updating the set of available tid's
    available_tids.erase(available_tids.begin());
    min_tid = *available_tids.begin();
    char *stack = new char[STACK_SIZE];
    thread_to_add->set_stack(stack);
    // _env = &buf;
    // Calculating sp and pc
    address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;
    // Saving the thread
    sigsetjmp(thread_to_add->_env, 1);
    ((thread_to_add->_env)->__jmpbuf)[JB_SP] = translate_address(sp);
    ((thread_to_add->_env)->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&(thread_to_add->_env)->__saved_mask);
    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
    // Returning the tid of the new thread we created
    return used_id;
}

/**
 * This function checks if a given thread is inside a given thread list, if so,
 * we terminate it.
 * @param tid The id of the thread we want to terminate.
 * @param thread_list The thread list from which we want to terminate a thread.
 * @return 0 upon success, -1 upon failure.
 */
int uthread_terminate_thread(unsigned int tid, std::deque<Thread *> *thread_list) {
    // Looping over all the threads in the given list
    for (auto it = thread_list->cbegin(); it != thread_list->cend(); it++) {
        Thread *thread = *it;
        // If the wanted thread was found
        if (thread->get_id() == tid) {
            thread_list->erase(it, it + 1);
            // delete tid_to_thread_map[(int) thread->get_id()];
            tid_to_thread_map.erase((int) tid);
            available_tids.insert((int) tid);
            min_tid = *available_tids.begin();

            thread->free_stack();
            delete thread;
            thread = nullptr;
            return 0;
        }
    }
    return -1;
}

/**
 * This function checks if a given thread is the running thread, if so,
 * we terminate it.
 * @param tid The id of the thread we want to terminate.
 * @return The function does not return upon success, -1 upon failure.
 */
int uthread_terminate_running_thread(unsigned int tid) {
    if (current_running_thread->get_id() == tid) {
        tid_to_thread_map.erase((int) tid);
        available_tids.insert((int) tid);
        min_tid = *available_tids.begin();
        current_running_thread->set_state(TERMINATED);
        timer_handler(0);
        return 0;
    }
    return -1;
}

/**
 * Terminate a single thread.
 * @param tid The id of the thread we want to terminate.
 * @return 0 upon success, -1 upon failure (i.e. a thread with tid does not exist)
 */
int uthread_terminate_single_thread(unsigned int tid) {
    if (!uthread_terminate_thread(tid, &ready_threads) ||
        !uthread_terminate_running_thread(tid) ||
        !uthread_terminate_thread(tid, &blocked_threads) ||
        !uthread_terminate_thread(tid, &sleeping_threads)) {
        return 0;
    }
    return -1;
}

int uthread_terminate(int tid) {
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);

    if (tid == MAIN_THREAD_TID) {
        for (int id = 1; id < MAX_THREAD_NUM; id++) {
            uthread_terminate(id);
        }
        delete current_running_thread;
        current_running_thread = nullptr;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        exit(0);
    } else {
        int ret_val = uthread_terminate_single_thread((unsigned int) tid);
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return ret_val;
    }
}

int uthread_block(int tid) {
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);
    // Thread with id tid does not exist
    if (available_tids.find(tid) != available_tids.end()) {
        std::cerr << THREAD_LIB_ERR << INVALID_TID << std::endl;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return -1;
    }
    // The thread is the main thread
    if (tid == MAIN_THREAD_TID) {
        std::cerr << THREAD_LIB_ERR << INVALID_MAIN_BLOCK << std::endl;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return -1;
    }
    // The thread tries to block itself
    if (current_running_thread->get_id() == (unsigned int)tid) {
        current_running_thread->set_state(BLOCKED);
        blocked_threads.push_back(current_running_thread);
        timer_handler(0);
    }
    // The thread tries to block a ready thread
    if (tid_to_thread_map[tid]->get_state() == READY) {
        tid_to_thread_map[tid]->set_state(BLOCKED);
        blocked_threads.push_back(tid_to_thread_map[tid]);
        auto it = std::find(ready_threads.begin(), ready_threads.end(), tid_to_thread_map[tid]);
        if (it != ready_threads.end()){
            ready_threads.erase(it);
        }
    }

    // The thread tries to block a sleeping thread
    if (tid_to_thread_map[tid]->get_state() == SLEEPING) {
        tid_to_thread_map[tid]->set_state(SLEEPING_AND_BLOCKED);
        blocked_threads.push_back(tid_to_thread_map[tid]);
    }

    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
    return 0;
}

int uthread_resume(int tid){
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);
    // Thread with id tid does not exist
    if (available_tids.find(tid) != available_tids.end()) {
        std::cerr << THREAD_LIB_ERR << INVALID_TID << std::endl;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return -1;
    }
    // The running thread tries to resume a blocked and sleeping thread
    if(tid_to_thread_map[tid]->get_state() == SLEEPING_AND_BLOCKED){
        tid_to_thread_map[tid]->set_state(SLEEPING);
    }
    // The running thread tries to resume a blocked thread
    if(tid_to_thread_map[tid]->get_state() == BLOCKED){
        tid_to_thread_map[tid]->set_state(READY);
        ready_threads.push_back(tid_to_thread_map[tid]);
    }

    auto it = std::find(blocked_threads.begin(), blocked_threads.end(), tid_to_thread_map[tid]);
    if (it != blocked_threads.end()) {
        blocked_threads.erase(it);
    }

    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
    return 0;
}

int uthread_sleep(int num_quantums){
    // Masking the timer signal
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);

    // If the given quantum usecs number is not valid
    if (num_quantums == 0) {
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return 0;
    }

    if (num_quantums < 0) {
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        std::cout << THREAD_LIB_ERR << std::endl;
        return -1;
    }

    if(current_running_thread->get_id() == MAIN_THREAD_TID) {
        std::cerr << THREAD_LIB_ERR << INVALID_MAIN_SLEEP << std::endl;
        sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
        return -1;
    }
    current_running_thread->set_state(SLEEPING);
    sleeping_threads.push_back(current_running_thread);
    current_running_thread->set_sleeping_quantum(num_quantums +1);
    timer_handler(0);
    sigprocmask(SIG_UNBLOCK, &signal_set, nullptr);
    return 0;
}

int uthread_get_tid(){
    return (int) current_running_thread->get_id();
}

int uthread_get_total_quantums(){
    return quantum_counter;
}

int uthread_get_quantums(int tid){
    return tid_to_thread_map[tid]->get_quantum_num();
}

