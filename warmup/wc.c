#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

struct word_count{
	char * word;
	int count;
	//point to next entry in case of a clash -> for chaining
	struct word_count * next;
};

struct wc {
	/* you can define this struct to have whatever fields you want. */
	int size;
	struct word_count** mapping;
};

long hash_value(char * str){
	long hash = 5381;
    int c;
	//printf("Hashing...\n");
    while ((c = *str++)){
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	//printf("Done hashing...\n");
    return hash;
}
//pass a reference to the hash table
void insert_into_wc(struct wc **wc, char * word){
	//printf("Start of insert\n");
	long hash = hash_value(word) % (*wc)->size;
	//make it positive
	if (hash < 0){
		hash = hash + (*wc)->size;
	}
	//printf("%d, %ld\n",(*wc)->size, hash);
	//print
	//create the Node
	if ((*wc)->mapping[hash] == NULL){
		//printf("First new entry in hash\n");
		//create the node
		(*wc)->mapping[hash] = malloc(sizeof(struct word_count));
		(*wc)->mapping[hash]->next = NULL;
		(*wc)->mapping[hash]->count = 1;
		(*wc)->mapping[hash]->word = word;
	}
	//something is here, so we chain it
	else{
		//printf("There is an entry here..\n");
		//check if already in the table
		struct word_count * current = (*wc)->mapping[hash];
		bool found = false;
		while(current){
			//if we find it
			//printf("loop through:%s, %s, %d\n", current->word, word, current->count);
			if (strcmp(current->word, word) == 0){
				//printf("Found in chain\n");
				current->count += 1;
				found = true;
				break;
			}
			//NULL is seen, end of list
			//printf("Before check\n");
			if (!current->next) break;
			//printf("Advancing...\n");
			current = current->next;
		}
		//if not found, insert it
		if (!found){
			//printf("Adding to end of chain\n");
			struct word_count * to_add = malloc(sizeof(struct word_count));
			to_add->next = NULL;
			to_add->count = 1;
			to_add->word = word;
			//aadd it to the chaining list
			current->next = to_add;
		}
	}
	//printf("Done\n");
}

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;
	char * copy = malloc(sizeof(char)*size); 
	strcpy(copy, word_array);

	int i = 0;
	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	//set the fields properly
	wc->size = 2*size;
	wc->mapping = malloc(wc->size * sizeof(struct word_count *));
	//initialize list to NULL
	for (i = 0; i < wc->size; i++){
		wc->mapping[i] = NULL;
	}

	assert(wc->mapping);
	// int last_index = 0;
	// int curr_index = 0;
	//parse the word array, split into words
	// for (i = 0; word_array[i] != '\0'; i++){//
	// 	if (isspace(word_array[i])){
	// 		int size_word = curr_index - last_index;
	// 		if (size_word > 0){
	// 			char * copied = malloc(sizeof(char)*size_word);
	// 			strncpy(copied, word_array+last_index, size_word);
	// 			insert_into_wc(&wc, copied);
	// 			//printf("%d, %s\n", size_word, copied);
	// 			//deal with the word
	// 			last_index = curr_index;
	// 		} 
	// 		last_index += 1;
	// 	}		
	// 	curr_index += 1;
	// }
	char delim[] = " \t\n\r\v\f";
	//printf("Delim: %s\n", delim);
	char *ptr = strtok(copy, delim);
	//printf("Heyo\n");
	while(ptr != NULL)
	{
		//printf("'%s'\n", ptr);
		insert_into_wc(&wc, ptr);
		ptr = strtok(NULL, delim);
	}
	return wc;
}

void
wc_output(struct wc *wc)
{
	//printf("Hi!\n");
	int size = wc->size;
	int i = 0;
	for (i = 0; i < size; i++){
		//is there an entry here
		struct word_count * curr = wc->mapping[i];
		//loop through all entries chained together, if applicable
		while (curr){
			printf("%s:%d\n", curr->word, curr->count);
			curr = curr->next;
		}
	}
}

void
wc_destroy(struct wc *wc)
{
	int size = wc->size;
	int i = 0;
	//deallocate each structure from the bottom
	for (i = 0; i < size; i++){
		//is there an entry here
		struct word_count * curr = wc->mapping[i];
		//loop through all entries chained together, if applicable
		while (curr){
			struct word_count *next = curr->next;
			//de-allocate the word
			//free(curr->word);
			//de-allocate the word_count structure
			free(curr);
			//printf("%s:%d\n", curr->word, curr->count);
			curr = next;
		}
	}
	free(wc);
}
