#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include <string.h>
#include "thread.h"
#include "interrupt.h"

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

typedef enum ThreadState{
	NOT_ASSIGNED = 0,
	READY = 1,
	RUNNING = 2
} ThreadState;  
/* This is the thread control block */
struct thread {
	/* ... Fill this in ... */
	ThreadState t_state;
	Tid id;
	ucontext_t mycontext;  //get it using getcontext, then change the sp, base pointer, the pc
	void * stack_to_free;  
	volatile int setcontext_called;
	volatile int killOnNextRun; 
	//volatile int freeTidStackOnNextRun;  
};
//defined the thread control block
struct thread TCBs[THREAD_MAX_THREADS];  
Tid current_thread_id = -1;

//will store a linkedlist of Threads ready to run 
struct ready_elem{
		Tid id;
		struct ready_elem * next;
		struct ready_elem * prev;
};
struct ready_queue{
	struct ready_elem * head;
	struct ready_elem * tail;
};
//readQueue will store all threads waiitng to run
struct ready_queue readyQueue;


//methods to control the insertion and deletion of elements
void addToHead(struct ready_queue * LL, Tid id){
	struct ready_elem * head = LL->head;
	struct ready_elem * tail = LL->tail; 
	
	//create the new entry
	struct ready_elem* newElem = malloc(sizeof(struct ready_elem));
	newElem->id = id;
	newElem->prev = NULL;
	newElem->next = NULL;
	//handle the two cases
	if (!head){
	    head = newElem;
	    tail = head;
	}
	else{
	    newElem->next = head;
	    head->prev = newElem;
	    head = newElem;
	}
	//update the pointers directly
	LL->head = head;
	LL->tail = tail;
}

void addToTail(struct ready_queue *LL, Tid id){
	struct ready_elem * head = LL->head;
	struct ready_elem * tail = LL->tail;
	
	//create the new entry
	struct ready_elem* newElem = malloc(sizeof(struct ready_elem));
	newElem->id = id;
	newElem->prev = NULL;
	newElem->next = NULL;

	if (!tail){ //if tail is null, then so is head
		tail = newElem;
		head = newElem;
	}
	else{
		// struct ready_elem * old_tail = tail;
		tail->next = newElem;
		newElem->prev = tail;
		tail = newElem;
	}
	LL->head = head;
	LL->tail = tail;	
}

Tid removeNextReadyThread(struct ready_queue *LL){
	struct ready_elem * head = LL->head;
	if (head){
		if (head->prev == NULL && head->next == NULL){
			LL->head = NULL;
			LL->tail = NULL;
			int ret_val = head->id;
			free(head);
			head = NULL;
			return ret_val;
		}
		else{
			//remove head
			struct ready_elem *to_remove = head;
			int ret_val = to_remove->id;
			LL->head = head->next;
			head->next->prev = NULL;
			free(to_remove);
			to_remove = NULL; //remove dangling pointer
			return ret_val;
		}
		
	}
	return -1; //no thread is ready to run
	
}
Tid removeFromReadyQueue(struct ready_queue *LL, Tid id){
	struct ready_elem * head = LL->head;
	struct ready_elem * tail = LL->tail;
	if (head){
		if (head->id == id){
		    if (head->prev == NULL && head->next == NULL){
				int ret_val = head->id;
		        LL->head = NULL;
		        LL->tail = NULL;
		        free(head);
		        head = NULL;
				return ret_val;
		    }
		    else{
    			//remove head
    			struct ready_elem *to_remove = head;
				int ret_val = to_remove->id;
    			LL->head = head->next;
    			head->next->prev = NULL;
    			free(to_remove);
    			to_remove = NULL; //remove dangling pointer
				return ret_val;
		    }
		}
		else if(tail->id == id){
		    if (tail->prev == NULL && tail->next == NULL){
				int ret_val = tail->id;
		        LL->head = NULL;
		        LL->tail = NULL;
		        free(tail);
		        tail = NULL;
				return ret_val;
		    }
		    else{
    			//remove tail
    			struct ready_elem *to_remove = tail;
				int ret_val = to_remove->id;
    			LL->tail = tail->prev;
    			tail->prev->next = NULL;
    			free(to_remove);
    			to_remove = NULL; //remove dangling pointer
				return ret_val;
		    }
		}
		else{
			//remove element in the middle, guaranteed to have an element after and before
			struct ready_elem *curr = head;
			while (curr){
				if (curr->id == id){
					struct ready_elem *to_remove = curr;
					int ret_val = to_remove->id;
					curr->prev->next = curr->next;
					curr->next->prev = curr->prev;
					free(to_remove);
					to_remove = NULL; //remove dangling pointer 
					return ret_val;
					//break;
				}
				curr = curr->next;
			}
			return -1;

		}
	}
	return -1;
}

void printList(struct ready_queue queue){
    printf("Forwards\n");
    struct ready_elem*head = queue.head;
    
    while (head){
        printf("val = %d\n", head->id);
        head = head->next;
    }
    printf("\nBackwards\n");
    struct ready_elem*tail = queue.tail;
    
    while (tail){
        printf("val = %d\n", tail->id);
        tail = tail->prev;
    }
    printf("\n");
}
//build the infrastructre to show all available TIDs
struct availableIdElem {
	Tid id;
	struct availableIdElem * next;	
};

struct availableIds {
	struct availableIdElem *head;
};

//this will keep track of the tids available to be assigned
struct availableIds availableThreadIds;

void addToHeadId(struct availableIds * LL, Tid id){
	struct availableIdElem * head = LL->head;
	struct availableIdElem * newElem = malloc(sizeof(struct availableIdElem));
	newElem->id = id;
	newElem->next = NULL;

	//append to head
	newElem->next = head;
	LL->head = newElem;
}

bool isIdNotUsed(struct availableIds LL, Tid id){
	struct availableIdElem * curr = LL.head;
	while (curr){
		if (curr->id == id){
			return true;
		}
	}
	return false;
}

Tid removeFromHead(struct availableIds * LL){
	struct availableIdElem * head = LL->head;
	if (head){
		//remove from head
		Tid id = head->id;
		LL->head = head->next;
		free(head);
		head = NULL;
		return id;	
	}
	else{
		return -1;
	}
}


void
thread_init(void)
{
	//initialize the ready queue
	readyQueue.head = NULL;
	readyQueue.tail = NULL;
	//initialize all available ids, fill LL with all options for ids
	availableThreadIds.head = NULL;
	int i = 0;
	for (i = 1023; i >= 1; i--){
		addToHeadId(&availableThreadIds, i);
		TCBs[i].id = i;
		// initialize the elements in by TCB (thread control block) for each thread that is not our main thread (id = 0)
		memset(&(TCBs[i].mycontext), 0, sizeof(ucontext_t));
		TCBs[i].stack_to_free = NULL;
		TCBs[i].t_state = NOT_ASSIGNED;
		TCBs[i].setcontext_called = 0;
		TCBs[i].killOnNextRun = 0;
		
	}
	//initialize Tid = 0 for the current thread
	TCBs[0].id = 0;
	memset(&(TCBs[0].mycontext), 0, sizeof(ucontext_t));
	TCBs[0].stack_to_free = NULL;
	TCBs[0].t_state = RUNNING;
	TCBs[0].setcontext_called = 0; 
	TCBs[0].killOnNextRun = 0;
	
	//initialize the current thread
	current_thread_id = 0;

}

void freeTCBStacks(){
	int i = 0;
	for (i = 1; i < THREAD_MAX_THREADS; i++){
		//if the stack has not yet been deallocated, deallocate it
		if (TCBs[i].stack_to_free && TCBs[i].t_state == NOT_ASSIGNED){
			free(TCBs[i].stack_to_free);
			TCBs[i].stack_to_free = NULL;
		}
	}
}

void threadResponsibilities(){
	//first free the allocated, unassigned stacks, then exit 
	freeTCBStacks(); 
	//exit if you were killed by another thread
	if (TCBs[current_thread_id].killOnNextRun == 1){
		thread_exit();
	}
}

Tid
thread_id()
{
	if (current_thread_id >= 0){   
		return current_thread_id;
	}
	return THREAD_INVALID;
}

void
thread_stub(void (*thread_main)(void *), void *arg)
{
	threadResponsibilities(); 
	// call thread_main() function with arg
	thread_main(arg); 
	//exit once done
	thread_exit();
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int err = getcontext(&TCBs[current_thread_id].mycontext);
	assert(!err);

	//get tid for this new thread, if any are available
	Tid id = removeFromHead(&availableThreadIds);
	//Check if all tids are being used 
	if (id == -1){ 
		return THREAD_NOMORE;
	}

	/* Deal with stack allocation, deallocation for the new not running thread */

	// if stack point is not NULL (allocated stack), then deallocate it
	if (TCBs[id].stack_to_free){
		free(TCBs[id].stack_to_free); //Note: It isn't safe to free the stack when the thead is running
		TCBs[id].stack_to_free = NULL;
	}
	//malloc a stack
	TCBs[id].stack_to_free = malloc(THREAD_MIN_STACK);  

	void * sp = TCBs[id].stack_to_free;
	
	if (!TCBs[id].stack_to_free){
		return THREAD_NOMEMORY;
	}
	//make the mycontext copy
	TCBs[id].mycontext = TCBs[current_thread_id].mycontext;
	
	//	change pc to pc of the stub fcn then update sp and bp, along with rid, rsi, note: stack base pointer is not necessary to fix for Lab 2 to work
	TCBs[id].mycontext.uc_mcontext.gregs[REG_RIP] = (unsigned long)thread_stub;
	
	//pass the fcn arsg to the appropriate reg (RDI) so the stub fcn can use them
	TCBs[id].mycontext.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn; 
	TCBs[id].mycontext.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg;
	TCBs[id].t_state = READY;
	//set these signal stack flags
	TCBs[id].mycontext.uc_stack.ss_sp = TCBs[id].stack_to_free;
	TCBs[id].mycontext.uc_stack.ss_size = THREAD_MIN_STACK;
	TCBs[id].mycontext.uc_stack.ss_flags = 0; 
	//align stack to 16 bytes 
	sp += TCBs[id].mycontext.uc_stack.ss_size;
	sp -= (unsigned long long)sp%16;
	sp -= 8;
	//set the stack pointer
	TCBs[id].mycontext.uc_mcontext.gregs[REG_RSP] = (unsigned long)sp;

	//	push this ready Thread with tid to readyQueue
	addToTail(&readyQueue, id); 
	//printf("Created thread with tid=%d\n", id);
	return id; 

}

Tid
thread_yield(Tid want_tid)
{
	Tid took_control = want_tid;
	int oldThreadId;
	//check against an invalid tid to yield to
	if (want_tid >= THREAD_MAX_THREADS || want_tid < -7){
		return THREAD_INVALID;
	}
	//check if we are trying to yield ourselves, not possible
	else if (THREAD_SELF == want_tid){
		return current_thread_id;
	}
	//check if we are trying to yield ourselves by specifying our current tid
	else if (want_tid == current_thread_id){
		return current_thread_id;
	}
	//want_tid doesn't have an active thread
	else if (want_tid >= 0 && TCBs[want_tid].t_state == NOT_ASSIGNED){
		//printf("Unassigned want_tid: %d\n", want_tid);
		return THREAD_INVALID;
	}
	else if (want_tid == THREAD_ANY){
		
		//select next READY thread, if any are available
		Tid nextTidToRun = removeNextReadyThread(&readyQueue);
		//printf("curr tid = %d, next tid: %d, setcontext_called: %d\n", thread_id(), nextTidToRun, TCBs[current_thread_id].setcontext_called);
		//None are available, keep 'current_thread_id' as is
		if (nextTidToRun == -1){
			return THREAD_NONE;
		}
		//printf("Tid = %d, setcontextcalled = %d\n", thread_id(), TCBs[current_thread_id].setcontext_called);
		//keep track of the tid that took control and the entering tid
		took_control = nextTidToRun;
		oldThreadId = current_thread_id;
		//update the state of current_thread
		int err_getcontext = getcontext(&TCBs[current_thread_id].mycontext);
		assert(!err_getcontext);
		//this is where we check if killThread was called for this tid once its state has been restored + do some stack freeing
		threadResponsibilities(); 

		//change the state of the calling thread appropriately such that it can yield to the nextThread we got from the ready queue
		if (TCBs[current_thread_id].setcontext_called == 0){
			//change old thread to the ready state  
			TCBs[current_thread_id].t_state = READY; 
			//put current_threaf to the back of the READY_QUEUE
			addToTail(&readyQueue, current_thread_id); 
			//update these for the next thread
			current_thread_id = nextTidToRun;
			//set the state of the next thread
			TCBs[nextTidToRun].t_state = RUNNING;  
			//update the yielding thread's setcontext_called to 1 (so that we don't have to re-enter this yielding chunk of code, cuz we already yielded once)
			TCBs[oldThreadId].setcontext_called = 1; 
			int err = setcontext(&TCBs[nextTidToRun].mycontext);
			//should never reach this point, setcontext doesnt return on success    
			assert(!err);
			assert(0);
		}
		//printf("Resetting oldId = %d, cur_thread = %d\n", oldThreadId, thread_id());
		//turn off setcontext_called upon calling thread being scheduled again, since we already executed the yielding block above, allows the calling thread to yield again in the future
		TCBs[current_thread_id].setcontext_called = 0; 
		
	}
	else{
		//Overall goal: make the requested tid active, yield current thread 

		//select next READY thread, if any are available
		int nextTidToRun = removeFromReadyQueue(&readyQueue, want_tid);
		
		//None are available, keep 'current_thread_id' as is
		if (nextTidToRun == -1){
			return THREAD_NONE;
		}
		
		//keep track of the tid that took control and the entering tid
		took_control = nextTidToRun;
		oldThreadId = current_thread_id;
		
		//update the state of current_thread
		int err_getcontext = getcontext(&TCBs[current_thread_id].mycontext); 
		assert(!err_getcontext);
		//this is where we check if killThread was called for this tid once its state has been restored + do some stack freeing
		threadResponsibilities();
		
		//change the state of the calling thread appropriately such that it can yield to the nextThread we got from the ready queue
		if (TCBs[current_thread_id].setcontext_called == 0){
			//change old thread to the ready state  
			TCBs[current_thread_id].t_state = READY;   
			//put current_threaf to the back of the READY_QUEUE
			addToTail(&readyQueue, current_thread_id); 
			//update these for the next thread
			current_thread_id = nextTidToRun;
			//set the state of the next thread
			TCBs[nextTidToRun].t_state = RUNNING;  
			//update the yielding thread's setcontext_called to 1 (so that we don't have to re-enter this yielding chunk of code, cuz we already yielded once)
			TCBs[oldThreadId].setcontext_called = 1; 
			int err = setcontext(&TCBs[nextTidToRun].mycontext);
			//should never reach this point, setcontext doesnt return on success    
			assert(!err);
			assert(0);
		}
		
		//turn off setcontext_called upon calling thread being scheduled again, since we already executed the yielding block above, allows the calling thread to yield again in the future
		TCBs[current_thread_id].setcontext_called = 0; 
	}
	
	return took_control;
}

void
thread_exit()
{
	//printf("Thread is exiting: %d\n", thread_id());

	//reset the context of this thread, change state to not assigned, remove from readyQueue
	memset(&(TCBs[current_thread_id].mycontext), 0, sizeof(ucontext_t));
	//WILL DEAL WITH FREEING STACK DURING RESTORE FROM YIELD, A THREAD'S FIRST RUN, & THE MAIN THREAD's EXIT
	TCBs[current_thread_id].t_state = NOT_ASSIGNED;
	TCBs[current_thread_id].killOnNextRun = 0;
	
	//make curr_tid (current_thread_id) available for future threads to use
	addToHeadId(&availableThreadIds, current_thread_id);
	
	//schedule next ready thread, if any are available
	Tid nextTidToRun = removeNextReadyThread(&readyQueue);
	
	//no thread is ready to run, this means we finish our execution
	if (nextTidToRun == -1){
		freeTCBStacks();
		exit(0);
	}
	
	//mark this nextTid as the new thread that is running
	current_thread_id = nextTidToRun;
	TCBs[nextTidToRun].t_state = RUNNING;
	
	int err = setcontext(&TCBs[nextTidToRun].mycontext); 
	//should never reach this point 
	assert(!err);
	assert(0);
}

Tid
thread_kill(Tid tid)
{
	//check if thread_kill is trying to commit suicide; if so: THREAD_INVALID to kill
	//printf("Not past the first if: isAssigned: %d\n", TCBs[tid].t_state);
	if (tid >= 1024 || tid < -7){
		return THREAD_INVALID;
	}
	//check if the target thread: tid isnt being used; if so: THREAD_INVALID to kill
	if ((tid == current_thread_id) || (TCBs[tid].t_state == NOT_ASSIGNED)){
		return THREAD_INVALID;
	}

	//mark the targeted tid such that it should exits the next time it runs
	TCBs[tid].killOnNextRun = 1;
	
	//keep 'current_thread_id' as is
	return tid;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
