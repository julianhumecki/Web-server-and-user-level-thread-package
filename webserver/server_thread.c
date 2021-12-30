#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <stdbool.h>

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
};

pthread_t *thread_pool;
int num_threads_in_pool;
//condition variables and mutex regions
pthread_cond_t cv_work;
pthread_cond_t cv_main; 
pthread_mutex_t mp; 

struct queue_struct {
	int connfd;
	struct server *sv;
	struct queue_struct * next; 
};
struct queue {
	struct queue_struct * head;
	struct queue_struct * tail;
	unsigned int length;
	int MAX_SIZE;
};
struct queue connection_requests;
bool isServerExiting = false;

bool atMaxCapacity(struct queue cn_rqts){
	return (cn_rqts.length == cn_rqts.MAX_SIZE);
}
bool pushConnection (struct queue * cn_rqts, struct server *sv, int connfd){
	struct queue_struct * head = cn_rqts->head;
	struct queue_struct * newElem = Malloc(sizeof(struct queue_struct));
	//initialzie the new elem
	newElem->sv = sv;
	newElem->connfd = connfd;
	newElem->next = NULL;

	if (cn_rqts->length < cn_rqts->MAX_SIZE){
		//if head is NULL, set the tail and head pointers
		if (!head) {
			cn_rqts->head = newElem;
			cn_rqts->tail = newElem;
		}
		else{
			//fix up the tail pointer
			struct queue_struct * tail = cn_rqts->tail;
			tail->next = newElem;
			cn_rqts->tail = tail->next;
		}
		cn_rqts->length++;
		return true;
	}
	return false;
}
struct queue_struct popConnection (struct queue * cn_rqts){
	struct queue_struct * head = cn_rqts->head;
	if (head){
		cn_rqts->head = head->next;
		if (!cn_rqts->head){
			cn_rqts->tail = NULL;
		}
		struct queue_struct ret_str = *head;
		//deallocate
		free(head);
		head = NULL;
		//reduce curr_length
		cn_rqts->length --;
		return ret_str;
	}
	//return an unable to pop structure
	struct queue_struct ret_nill;
	ret_nill.connfd = -1;
	ret_nill.sv = NULL;
	ret_nill.next = NULL;
	return ret_nill;
}
/*struct queue_struct * peak(struct queue *cn_rqts){
	return cn_rqts->head;
}*/

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	ret = request_readfile(rq);
	if (ret == 0) { /* couldn't read file */
		goto out;
	}
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
}

void *
do_server_t_request(void * ptr){
	//the while loop creates the thread pool effect (if task is available, the thread does it, else it waits)
	//printf("Entering the thread pool: id = %ld\n", pthread_self()); 
	while (1) {
		//control access to the connection_request_modification
		pthread_mutex_lock(&mp);
		struct queue_struct front = popConnection(&connection_requests);
		//printf("Initial task being requested: connfd = %d, id = %ld, curr_len=%d\n", front.connfd, pthread_self(), connection_requests.length);
		//get signaled when we add a task to the queue
		//another thread could beat us to the resource, meaning we don't get a task
		//this is possible since a thread could be waiting on acquiring the lock on line 149, and it could get the be in front of the cond_waiting thread in the lock_wait queue
		//thus, this must be a while loop
		while (front.connfd == -1){ 
			pthread_cond_wait(&cv_work, &mp);
			//printf("THread woke up, id = %ld, curr_len = %d\n", pthread_self(), connection_requests.length);
			//try again to get a socket identifier to send the data back to a client
			front = popConnection(&connection_requests);
			//don't wait again, just leave
			if (isServerExiting){
				break;
			}
		}
		//printf("After task has been retrieved: front connfd = %d, isServerExiting = %d, curr_len = %d\n", front.connfd, isServerExiting, connection_requests.length); //should  never be -1
		/*if (front.connfd == -1){
			printf("Should never be reached\n");
		}*/
		//signal main that we have removed a task, so the desired task can be added to the queue
		pthread_cond_signal(&cv_main);
		//remove thread from thead pool
		if (isServerExiting){
			pthread_mutex_unlock(&mp);
			break; 
		}
		//will be reached if we aren't exiting
		pthread_mutex_unlock(&mp);

		//printf("Fulfilling work request: connfd = %d\n", front.connfd);
		//uses a read value that was obtain in a mutex, safe to read from
		do_server_request(front.sv, front.connfd);	 
		//printf("Fulfilled work request: connfd = %d\n", front.connfd);
	}
	return NULL;
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;
	//printf("Server started...\n");

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;

	//initialize the synchronization & mutex vars
	assert(!pthread_mutex_init(&mp, NULL));
	assert(!pthread_cond_init(&cv_work, NULL));
	assert(!pthread_cond_init(&cv_main, NULL));
	
	/* Lab 4: create queue of max_request size when max_requests > 0 */
	if (max_requests > 0){
		connection_requests.head = NULL; 
		connection_requests.tail = NULL;
		connection_requests.length = 0;
		connection_requests.MAX_SIZE = max_requests;
	}
	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */
	num_threads_in_pool = nr_threads;
	if (nr_threads > 0){
		thread_pool = Malloc(nr_threads * sizeof(pthread_t));
		//create the pool of worker threads
		int i = 0;
		for (i = 0; i < nr_threads; i++){
			pthread_create(&thread_pool[i], NULL, do_server_t_request, NULL); 
		}
	}

	return sv;
}

//main thread will only ever enter this, eventually will be able to push the task, no deadlocks
void
server_request(struct server *sv, int connfd)
{
	//printf("Incoming server request: connfd = %d\n", connfd);
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&mp);
		bool pushSuccess = pushConnection(&connection_requests, sv, connfd);
		//queue buffer is full, so we must put the main thread to sleep using cv_wait
		while (!pushSuccess){ //while is also not necessary, guaranteed to have a free spot if signaled
			pthread_cond_wait(&cv_main, &mp); //main thread must also get signaled to wake up
			//try to add the task again
			pushSuccess = pushConnection(&connection_requests, sv, connfd);
		}
		//make sure it was pushed properly
		assert(connfd == connection_requests.tail->connfd);
		//printf("Main thread pushed: ")
		//signal to a waiting_threads that we have added a task, let one of them do it
		pthread_cond_signal(&cv_work);  
		//release the lock
		pthread_mutex_unlock(&mp);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	//printf("Servering exiting\n");
	sv->exiting = 1;
	//only do the following if worker threads were made
	if (sv->nr_threads > 0){
		//send a signal to all worker threads that we are done and to exit
		pthread_mutex_lock(&mp);
		isServerExiting = true;
		//wake up all idle threads
		pthread_cond_broadcast(&cv_work);
		pthread_mutex_unlock(&mp);

		int i = 0;
		//will block the main thread until each worker thread exits
		for (i = 0; i < sv->max_requests; i++){
			pthread_join(thread_pool[i], NULL);
		}
		free(thread_pool);
		thread_pool = NULL;
	}
	/* make sure to free any allocated resources */
	free(sv);
	//printf("Server successfully exited\n"); 
	
}
