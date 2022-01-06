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
pthread_mutex_t cache_access;
pthread_mutex_t inflight_access;

//-------------------------------------------------------------------------------------
//LINKEDLIST FOR MANAGING CONNECTION REQUESTS
//-------------------------------------------------------------------------------------
struct queue_struct {
	int connfd;
	struct server *sv;
	struct queue_struct * next; 
};
struct sched_queue{
	char *filename;
	pthread_mutex_t *file_lock;
	pthread_cond_t *file_cv; 
	struct sched_queue * next;
};
struct queue {
	struct queue_struct * head;
	struct queue_struct * tail;
	unsigned int length;
	int MAX_SIZE;
};
struct scdle {
	struct sched_queue * head;
	struct sched_queue * tail;
};
struct queue connection_requests;
//stores the files that are being sent to the client
struct scdle inflight_q;

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
//SCHEDULING QUEUE
struct sched_queue * add_to_queue (struct scdle * sch_queue, char * filename){  
	struct sched_queue * head = sch_queue->head;
	struct sched_queue * newElem = Malloc(sizeof(struct sched_queue));
	
	//initialzie the new elem
	newElem->filename = filename;
	newElem->file_lock = Malloc(sizeof(pthread_mutex_t));
	newElem->file_cv = Malloc(sizeof(pthread_cond_t));
	newElem->next = NULL;
	//initialize the lock and cv var
	assert(!pthread_mutex_init(newElem->file_lock, NULL));
	assert(!pthread_cond_init(newElem->file_cv, NULL));
		
	//if head is NULL, set the tail and head pointers
	if (!head) {
		sch_queue->head = newElem;
		sch_queue->tail = newElem;
	}
	else{
		//fix up the tail pointer
		struct sched_queue * tail = sch_queue->tail;
		tail->next = newElem;
		sch_queue->tail = tail->next;
	}
	return newElem;
}
struct sched_queue * add_to_queue_cpy(struct scdle * sch_queue, struct sched_queue * copy){
	struct sched_queue * newElem = Malloc(sizeof(struct sched_queue));
	newElem->filename = copy->filename;
	newElem->file_lock = copy->file_lock;
	newElem->file_cv = copy->file_cv;
	newElem->next = NULL;
	//tail
	struct sched_queue * tail = sch_queue->tail;
	tail->next = newElem;
	sch_queue->tail = newElem;

	return newElem;
}
struct sched_queue *isInFlight (struct scdle * sch_queue, char * filename){
	struct sched_queue * head = sch_queue->head;
	while (head){
		//printf("isInFlight filename: %s\n", filename);
		if (strcmp(head->filename, filename) == 0){
			return head;
		}
		head = head->next;
	}
	return NULL;
}
//do not deallocate the structure
struct sched_queue *remove_from_queue (struct scdle * sch_queue, char * filename){

	struct sched_queue * head = sch_queue->head;
	struct sched_queue * tail = sch_queue->tail;
	if (!head){
		//printf("List is empty, fail no removal\n");
		return NULL;
	}
	//printf("remove_from_queue filename: %s\n", filename);
	if (strcmp(head->filename, filename) == 0){
		sch_queue->head = head->next;
		if (!sch_queue->head){
			sch_queue->tail = sch_queue->head;
		}
		return head;
	}

	struct sched_queue * prev;
	prev = head;
	head = head->next;
	//printf("remove_from_queue2 filename: %s\n", filename);	
	while (head){
		if (strcmp(head->filename, filename) == 0){
			prev->next = head->next;
			if (head == tail){
				sch_queue->tail = prev;
			}
			return head;
		}
		prev = head;
		head = head->next;
	}
	//printf("Cannot be removed, dne\n");
	return NULL;
}

//implement the hash table for the cache & the doubly linked list for the LRU policy
//-------------------------------------------------------------------------------------
//DOUBLY LINKEDLIST FOR CACHE's LRU POLICY
//-------------------------------------------------------------------------------------
struct ready_elem{ 
		struct file_data * info; 
		struct ready_elem * next;
		struct ready_elem * prev;
};
struct cache_lru{
	struct ready_elem * head;
	struct ready_elem * tail;
};
//readQueue will store all threads waiitng to run
struct cache_lru LRU_Policy;

//add an element to the back of the linkedlist
bool pushBack(struct cache_lru *LL, struct file_data * data){
	struct ready_elem * head = LL->head;
	struct ready_elem * tail = LL->tail;
	
	//create the new entry
	struct ready_elem* newElem = Malloc(sizeof(struct ready_elem));
	newElem->info = data; //shallow copy since the deep copy is done by the insert in the hash table
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
	return true;
}
//return a cached file from the lru, slower than from cache, but it is less code!
struct file_data *getCachedFile(struct cache_lru *LL, struct file_data *data){
	struct ready_elem * head = LL->head;
	//printf("getCachedFile filename: %s\n", data->file_name);
	while (head){
		if (strcmp(head->info->file_name, data->file_name) == 0){
			return head->info;
		}
		head = head->next;
	}
	return NULL; 
}
struct file_data * removeLRUFile(struct cache_lru *LL){ 
	struct ready_elem * head = LL->head;
	if (head){
		if (head->prev == NULL && head->next == NULL){ //only one elem in the list
			LL->head = NULL;
			LL->tail = NULL;
			struct file_data * ret_val = head->info;
			head->info = NULL;

			free(head);
			head = NULL;
			return ret_val;
		}
		else{
			//remove head
			struct ready_elem *to_remove = head;
			struct file_data * ret_val = to_remove->info;
			to_remove->info = NULL;

			LL->head = head->next;
			head->next->prev = NULL;
			free(to_remove);
			to_remove = NULL; //remove dangling pointer
			return ret_val;
		}
		
	}
	return NULL; //no thread is ready to run 
}

struct file_data * removeFileToBeMovedToBack(struct cache_lru *LL, struct file_data * data){
	struct ready_elem * head = LL->head;
	struct ready_elem * tail = LL->tail;
	if (head){
		// printf("removeFileToBeMovedToBack filename: %s\n", data->file_name); 
		if (strcmp(head->info->file_name, data->file_name) == 0 ){
		    if (head->prev == NULL && head->next == NULL){
				struct file_data * ret_val = head->info;
		        LL->head = NULL;
		        LL->tail = NULL;
		        free(head);
		        head = NULL;
				return ret_val;
		    }
		    else{
    			//remove head
    			struct ready_elem *to_remove = head;
				struct file_data * ret_val = to_remove->info;
    			LL->head = head->next;

    			head->next->prev = NULL;
    			free(to_remove);
    			to_remove = NULL; //remove dangling pointer
				return ret_val;
		    }
		}
		else if(strcmp(tail->info->file_name, data->file_name) == 0 ){
		    if (tail->prev == NULL && tail->next == NULL){
				struct file_data * ret_val = tail->info;
		        LL->head = NULL;
		        LL->tail = NULL;
		        free(tail);
		        tail = NULL;
				return ret_val;
		    }
		    else{
    			//remove tail
    			struct ready_elem *to_remove = tail;
				struct file_data * ret_val = to_remove->info;
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
				if (strcmp(curr->info->file_name, data->file_name) == 0 ){
					struct ready_elem *to_remove = curr;
					struct file_data * ret_val = to_remove->info;
					curr->prev->next = curr->next;
					curr->next->prev = curr->prev;
					free(to_remove);
					to_remove = NULL; //remove dangling pointer 
					return ret_val;
				}
				curr = curr->next;
			}
			return NULL;

		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
//HASH TABLE FOR CACHING
//-----------------------------------------------------------------------------
struct word_count{
	struct file_data * data;
	//point to next entry in case of a clash -> for chaining
	struct word_count * next;
};

struct cache { 
	/* you can define this struct to have whatever fields you want. */
	int MAX_SIZE;
	int curr_size;
	int array_size;
	struct word_count** mapping;
};

struct cache *fileCache;

//the cache fcns
bool cache_lookup(struct file_data * data);
bool cache_insert(struct file_data * data);
bool cache_evict(struct cache**wc, struct cache_lru * LRU_Policy);


long hash_value(char * str){
	long hash = 5381;
    int c;
    while ((c = *str++)){
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	//printf("hash computed\n");
    return hash;
}
//pass a reference to the hash table
bool insert_to_cache(struct cache **wc, struct file_data * data){
	
	//an IMPOSSIBLE CASE to deal with
	if (data->file_size > (*wc)->MAX_SIZE){
		return false;
	}
    
	//obtain the hash of the file name
	long hash = hash_value(data->file_name) % (*wc)->array_size;
	
	//make the hash positive if it is negative
	if (hash < 0){
		hash = hash + (*wc)->array_size;
	}
	
	//create the Node if there is no element at this index
	if ((*wc)->mapping[hash] == NULL){
	    // printf("First entry for hash: %ld\n", hash);
		//add the file to the cache if there is enough room
		if ((*wc)->curr_size + data->file_size <= (*wc)->MAX_SIZE){
		    //allocate the hash table entry
			(*wc)->mapping[hash] = Malloc(sizeof(struct word_count));
			(*wc)->mapping[hash]->next = NULL;
			//allocate the file_data structure 
			(*wc)->mapping[hash]->data = data;
			
			//update the current size of the cache
			(*wc)->curr_size += data->file_size;
			// printf("Pushing back\n");
			//push back the new address of our copy of the data
			pushBack(&LRU_Policy, (*wc)->mapping[hash]->data);
			return true;
		}
		else{
			// printf("Evict is neccessary\n");
			//evict the neccessary entries in the doubly linked list (first n LRU unused elements to make room for the new file)
			while ((*wc)->curr_size + data->file_size > (*wc)->MAX_SIZE){
			    // printf("cache size: %d\n", (*wc)->curr_size);
				if ((*wc)->curr_size == 0){//evicted everything
				    // printf("cache is empty\n");
				    break;
				}
				cache_evict(wc, &LRU_Policy);
			}
			// printf("Done evicting\n");
			//insert into the cache the new file, base case will be hit
			return insert_to_cache(wc, data); 
		}
		
		
	}
	//there is a file here, so let's chain
	else{
	    // printf("Chaining: filename: %s\n", data->file_name);
		//check if file is already in the table
		struct word_count * current = (*wc)->mapping[hash];
		bool found = false;
		while(current){
			//if we find the file, no need to re-insert it
			if (strcmp(current->data->file_name, data->file_name) == 0 /*&& current->data->file_size == data->file_size*/){
				//file is already here
				return false;
			}
			//NULL is seen, end of list
			if (!current->next) break;
			current = current->next;
		}
		// printf("Notfound\n");
		//if not found, insert the file
		if (!found){
			//printf("Adding to end of chain\n");
			struct word_count * to_add = Malloc(sizeof(struct word_count));
			to_add->next = NULL;

			//add the file to the cache if there is enough room
			if ((*wc)->curr_size + data->file_size <= (*wc)->MAX_SIZE){
				struct word_count * to_add = Malloc(sizeof(struct word_count));
				to_add->next = NULL;
				//malloc the file_data structure
				to_add->data = data;
				
				(*wc)->curr_size += data->file_size;
				// printf("current->filename: %s\n", current->data->file_name);
				//add it to the end of the list
				current->next = to_add;
				//push to back of lru
				pushBack(&LRU_Policy, to_add->data);
				return true;
			}
			else{
				//will likely need to evict the first few entries in the doubly linked list (first n LRU unused elements to make room)
				while ((*wc)->curr_size + data->file_size > (*wc)->MAX_SIZE){
				    // printf("cache size: %d\n", (*wc)->curr_size);
					if ((*wc)->curr_size == 0){//evicted everything
					    // printf("cache is empty\n");
					    break;
					}
					cache_evict(wc, &LRU_Policy);
					
				}
				//insert into the cache the new file, base case will be hit
				return insert_to_cache(wc, data); 
				
			}
		}
	}
	return false;
}

bool remove_from_cache(struct cache **wc, struct file_data * data){
	//obtain the hash of the file name
	long hash = hash_value(data->file_name) % (*wc)->array_size;
	//make the hash positive if it is negative
	if (hash < 0){
		hash = hash + (*wc)->array_size;
	}

	//SYNCH to control the eviction of an inflight file

	pthread_mutex_lock(&inflight_access);
	struct sched_queue * inFlightFile = isInFlight(&inflight_q, data->file_name);
	//struct sched_queue * sendingToClient; 
	pthread_mutex_unlock(&inflight_access);

	while (inFlightFile){
		pthread_mutex_lock(inFlightFile->file_lock);
		//printf("Sleeping on file to evict: %s\n", inFlightFile->filename);
		pthread_cond_wait(inFlightFile->file_cv, inFlightFile->file_lock);
		pthread_mutex_unlock(inFlightFile->file_lock);
		//deallocate the structure
		free(inFlightFile->file_lock);
		free(inFlightFile->file_cv);
		free(inFlightFile);
		inFlightFile = NULL;
		//check if another instance of this file is in flight
		pthread_mutex_lock(&inflight_access);
		inFlightFile = isInFlight(&inflight_q, data->file_name);
		pthread_mutex_unlock(&inflight_access);
	}


	struct word_count * curr = (*wc)->mapping[hash];
	struct word_count * prev;

	//element is not in the cache
	if (!curr){
		return false;
	}
	//printf("remove_from_cache1 data->filename: %s\n", data->file_name);
	if (strcmp(curr->data->file_name, data->file_name) == 0 /*&& curr->data->file_size == data->file_size*/){  
		//adjust the head of the linkedlist, if the first element is a match
		(*wc)->mapping[hash] = curr->next; 
		//size to be added
		(*wc)->curr_size -= curr->data->file_size;
		//de-allocate the file entry
		//printf("Removing from cache: fileName = %s\n", curr->data->file_name);
		free(curr->data->file_name);
		free(curr->data->file_buf);
		free(curr->data);
		//de-allocate the word count structure
		free(curr); 
// 		(*wc)->mapping[hash] = NULL; 
		return true;
	}
	//else we havent found it yet, so let's loop through the rest of the list
	prev = curr;
	curr = curr->next;

	while (curr){
		//printf("remove_from_cache2 data->filename: %s\n", data->file_name);
		if (strcmp(curr->data->file_name, data->file_name) == 0 /*&& curr->data->file_size == data->file_size*/){
			prev->next = curr->next;
			//add the size back
			(*wc)->curr_size -= curr->data->file_size;
			//printf("Removing from cache: fileName = %s\n", curr->data->file_name);
			//de-allocate the file entry
			free(curr->data->file_name);
			free(curr->data->file_buf);
			free(curr->data);
			//de-allocate the word count structure
			free(curr);
			curr = NULL; 
			return true;
		}
		prev = curr;
		curr = curr->next; 
	}
	//we couldn't find the file in the hash's linkedlist traversal, thus return notFound
	return false;
}


struct cache *
init_cache(int maxSize)
{
	struct cache *wc;

	int i = 0;
	wc = (struct cache *)Malloc(sizeof(struct cache));
	assert(wc);

	//set the fields properly
	wc->array_size = 2*(maxSize/(1 << 12)); //number of elems in array: 2 * (maxCacheSize / 4KB (typical page size))
	//printf("Array size in hash table:%d \n", wc->array_size);  
	// wc->array_size = (maxSize/2);
	wc->curr_size = 0;
	wc->MAX_SIZE = maxSize;
	//initialize an array of linkedlist
	wc->mapping = Malloc(wc->array_size * sizeof(struct word_count *));
	//initialize list to NULL
	for (i = 0; i < wc->array_size; i++){
		wc->mapping[i] = NULL;
	}

	assert(wc->mapping);
	return wc;
}

void
print_cache(struct cache *wc)
{
    // printf("Cache printing\n");
    if (!wc) return;
    
	int size = wc->array_size;
	int i = 0;
	for (i = 0; i < size; i++){
		//is there an entry here
		struct word_count * curr = wc->mapping[i];
		//loop through all entries chained together, if applicable
		while (curr){
			// printf("%s:%d, buf = %s, hash = %d, addr=%p\n", curr->data->file_name, curr->data->file_size, curr->data->file_buf, i, curr->data);
			curr = curr->next;
		}
	}
}

void
destroy_cache(struct cache *wc)
{
	int size = wc->array_size;
	int i = 0;
	//deallocate each structure from the bottom
	for (i = 0; i < size; i++){
		//is there an entry here
		struct word_count * curr = wc->mapping[i];
		//loop through all entries chained together, if applicable
		while (curr){
			struct word_count *next = curr->next;
			//remove the front lru_data
			removeLRUFile(&LRU_Policy);
			//free space up in the cache
			(wc)->curr_size -= curr->data->file_size;
			//de-allocate the file entry
			// printf("Destorying: file_name = %s\n", curr->data->file_name);
			free(curr->data->file_name);
			free(curr->data->file_buf);
			free(curr->data);
			//de-allocate the word count structure
			free(curr);
			//advance to the next element
			curr = next;
		}
	}
	free(wc);
}
void destroy_cache_wrapper(){
    destroy_cache(fileCache);
    fileCache = NULL;
}
//cache fcns to implement:
//1) cache_lookup(file), 2) cache_insert(file), and 3) cache_evict(amount_to_evict)
bool cache_lookup(struct file_data * data){
	struct file_data * info = removeFileToBeMovedToBack(&LRU_Policy, data);
	//not found in LRU, thus not in cache
	if (!info) {/*printf("Lookup unsuccessful\n");*/return false;}
	//move data to the back so that it reflects that the file was looked up recently
	return pushBack(&LRU_Policy, info);
}
//adding to the  LRU queue is covered by insert_to_cache
bool cache_insert(struct file_data * data){
    // printf("Inserting to cache\n");
	return insert_to_cache(&fileCache, data);
}
bool cache_evict(struct cache**wc, struct cache_lru * LRU_Policy){
    // printf("Evicting from cache\n");
	struct file_data * elem = removeLRUFile(LRU_Policy);
	// printf("Removed from LRU filename: %s, now removing from cache\n", elem->file_name);
	return remove_from_cache(wc, elem); 
}
//TODO
//2) actually make use of the cache in: do_server_request, using proper mutex+synch primitives


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

	//deal with case of 0 cache size
	if (!fileCache){
		
		//set something to say we are reading the file & sending it 
		//check if we have cached this file -> yes, goto out
		//no: 
		//  read file, 
		//  * fills data->file_buf with the file contents,
		//  * data->file_size with file size. 
		ret = request_readfile(rq);
		if (ret == 0) { // couldn't read file 
			goto out_short;
		}
		// send file to client 
		request_sendfile(rq);
	out_short:
		request_destroy(rq);
		file_data_free(data);
		return;
	}
	//case 2) handles the mutex reading of a file, such that multiple copies of the same file dont get saved in the cache
	pthread_mutex_lock(&cache_access);
	//printf("Looking up connfd = %d, filename=%s\n", connfd, data->file_name);
	if (cache_lookup(data)){
		struct file_data * file = getCachedFile(&LRU_Policy, data);
		// rq->data = file; 
		//printf("File is cached: connfd=%d, filename=%s, cached file == NULL: %d\n", connfd, data->file_name, file==NULL);   
		/*if (file){
			printf("FIle cache name = %s\n", file->file_name);
		}*/
		request_set_data(rq, file);
		free(data);
		data = NULL;
		data = file;
		pthread_mutex_unlock(&cache_access);
	}
	else{
		pthread_mutex_unlock(&cache_access);
		//read the file in a mutex free zone
		ret = request_readfile(rq);
		if (ret == 0) { // couldn't read file 
			goto out;
		}
		pthread_mutex_lock(&cache_access);
		struct file_data * file;
		//if the file is already in the cache, then this thread lost the race
		if (cache_lookup(data)){
			file = getCachedFile(&LRU_Policy, data);
			//printf("Is file cached after race = %d\n", file==NULL);
			//update the pointer to the cached file for the request to send to the client
			// rq->data = file;
			request_set_data(rq, file);
			//printf("File is cached: connfd = %d, filename = %s\n", connfd, data->file_name);
			free(data);
			data = NULL;
			data = file;
		}
		//this thread won the race, so add its file to the cache
		else{
			//printf("Inserting to the cache, connfd=%d, filename=%s\n", connfd, data->file_name);
			//bool ret = cache_insert(data);
			cache_insert(data); 
			//printf("Inserted to the cache, success = %d\n", ret);
			//don't free this data, just send the request
		}
		pthread_mutex_unlock(&cache_access);
	}
	//case 1) handle the eviction of a file currently being sent to a client
	pthread_mutex_lock(&inflight_access);
	//printf("connfd = %d, filename=%s\n", connfd, data->file_name); 
	//check if this file is already in flight, if yes, add a copy of its lock and cv to the queue
	struct sched_queue * foundFile = add_to_queue(&inflight_q, data->file_name);
	//printf("after addtoqueue isfoundFile null = %d, connfd = %d, filename = %s, foundFile's filename = %s\n", foundFile == NULL, connfd, data->file_name, foundFile->filename);
	pthread_mutex_unlock(&inflight_access);

	// send file to client 
	request_sendfile(rq);

	pthread_mutex_lock(&inflight_access);
	//does not dealloc the structure, but removes it from the queue
	//printf("before removal isfoundFile null = %d, connfd = %d, filename = %s\n", foundFile == NULL, connfd, data->file_name);
	foundFile = remove_from_queue(&inflight_q, data->file_name);
	//printf("after removal isfoundFile null = %d, connfd = %d, filename = %s\n", foundFile == NULL, connfd, data->file_name);

	pthread_mutex_unlock(&inflight_access);
	//signal the sleeping thread in the eviction queue to wake up 
	pthread_mutex_lock(foundFile->file_lock);
	pthread_cond_signal(foundFile->file_cv); //signal once to indicate this instance of the file was sent
	pthread_mutex_unlock(foundFile->file_lock);
	goto done;

out:
	//printf("Deallocating the data, connfd: %d\n", connfd);
	request_destroy(rq);
	file_data_free(data);
done:
	//printf("Not Deallocating the data, connfd: %d\n", connfd);
	request_destroy(rq);
	return;

}

void *
do_server_t_request(void * ptr){
	//the while loop creates the thread pool effect (if task is available, the thread does it, else it waits)
	while (1) {
		//control access to the connection_request_modification
		pthread_mutex_lock(&mp);
		struct queue_struct front = popConnection(&connection_requests);
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
	assert(!pthread_mutex_init(&cache_access, NULL));
	assert(!pthread_mutex_init(&inflight_access, NULL));
	
	/* Lab 4: create queue of max_request size when max_requests > 0 */
	if (max_requests > 0){
		connection_requests.head = NULL; 
		connection_requests.tail = NULL;
		connection_requests.length = 0;
		connection_requests.MAX_SIZE = max_requests;
	}
	/* Lab 5: init server cache and limit its size to max_cache_size */
	if (max_cache_size > 0){
		LRU_Policy.head = NULL;
    	LRU_Policy.tail = NULL;
		fileCache = init_cache(max_cache_size);  
	}
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

		destroy_cache_wrapper();
	}
	/* make sure to free any allocated resources */
	free(sv); 
	//printf("Server successfully exited\n"); 
	
}
