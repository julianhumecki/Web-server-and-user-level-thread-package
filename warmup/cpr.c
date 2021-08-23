#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stdbool.h>
/* make sure to use syserror() when a system call fails. see common.h */

void copyOverFile(char * source, char *dest);
void dealWithDirectory(char *source, char *dest);
int isFile(const char* name);
char * getDirName(char * src);

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

int isFile(const char* name)
{
    DIR* directory = opendir(name);

    if(directory != NULL)
    {
     closedir(directory);
     return 0;
    }

    if(errno == ENOTDIR)
    {
     return 1;
    }

    return -1;
}

char * getDirName(char * src){
	int i = 0;
	//loop over source
	char *last_char;
	//bool last_is_backslash = false;
	int last_char_i = 0;
	for (i = 0; src[i] != '\0'; i++){
		if (src[i] == '/' && i != strlen(src)-1){
			last_char = &src[i+1];
			last_char_i = i+1;
		}
		// else if (i == strlen(src) - 1){
		// 	if (src[i] == '/'){
		// 		//last_is_backslash = true;
		// 	}
		// }
	}
	//this only helps if source path doesnt have a forward slash: /
	int size = strlen(src)-last_char_i + 1;
	//increment size appropriately
	//if (!last_is_backslash) size += 1;

	char * name = malloc(sizeof(char)*size);
	strcpy(name, last_char);
	//if not seen, add it in
	// if (!last_is_backslash){
	// 	strcat(name, "/");
	// 	printf("Added forward flash to source dir name: %s\n", name); 
	// } 

	//printf("Source dir name: %s, Grabbing: %s\n", src, name); 
	return name;

}

void dealWithDirectory(char *source, char *dest){
	//loop over contents
	//for elem in dir
	//  if elem isFile, copy it over
	//  else it is dir, call dealWithDir
	// create newDir to store everything made here, in case source doesn't end with the '/'
	int newSourceDirLen = strlen(source) + 12; //may need plus 1
	//check last char
	if (source[strlen(source) - 1] != '/'){
		newSourceDirLen += 1;
	}
	//create new path for source
	char * newDirForSrc = malloc(sizeof(char)*newSourceDirLen);
	strcpy(newDirForSrc, source);
	//append '/' if not at end of source string
	if (source[strlen(source) - 1] != '/'){
		strcat(newDirForSrc, "/"); 
		// printf("--> adding the forward slash to source: %s\n", newDirForSrc);
	}

	//build the destination directory name
	char * dirName = getDirName(newDirForSrc); //returns last dir of source: name/
	int newDirNameLen = strlen(dest) + strlen(dirName)+1;//maybe + 1
	//add a / to end of path if not there yet
	if (dest[strlen(dest) - 1] != '/'){
		newDirNameLen += 1;//increment
		// printf("Not ending in forward slash, Dest = %s\n", dest);
	}
	char * newDirForDest = malloc(sizeof(char)*newDirNameLen);
	strcpy(newDirForDest, dest);
	//append '/' if not at end of dest string
	if (dest[strlen(dest) - 1] != '/'){
		strcat(newDirForDest, "/");
	}
	strcat(newDirForDest, dirName);
	//make the directory
	int made = mkdir(newDirForDest, S_IWUSR|S_IRUSR|S_IXUSR|S_IROTH|S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP);
	//cant make it
	if (made == -1){
		// printf("Not made: %s, %s\n", newDirForDest, dirName);
		return;
	}
	else{
		// printf("Made DestPath: %s, DirName: %s\n", newDirForDest, dirName);
	}
	//open the directories
	DIR *d;
	DIR * d_dest;
	struct dirent *dir;
	d = opendir(newDirForSrc);
	d_dest = opendir(newDirForDest);

	if (d && d_dest) {
		while ((dir = readdir(d)) != NULL) {
			char *filename = dir->d_name;
			// printf("Filename: %s\n", filename);
			//fill in newPath for source
			int new_len = strlen(filename) + strlen(newDirForSrc)+1; 
			char * newPath = malloc(sizeof(char)*new_len);
			strcpy(newPath, newDirForSrc);
			strcat(newPath, filename);
			//printf("newSrcPath: %s\n", newPath);

			
			//check if file or directory 
			if (isFile(newPath) == 1){
				
				//append the name if it is a file to dest
				int new_len_dest = strlen(filename) + strlen(newDirForDest) + 1; 
				char * newPathDest = malloc(sizeof(char)*new_len_dest);
				strcpy(newPathDest, newDirForDest);
				// strcat(newPathDest, "/");
				strcat(newPathDest, filename);
				// printf("newSrcPath for file: %s\n", newPath);
				// printf("newPathDest for file: %s\n", newPathDest);
				copyOverFile(newPath, newPathDest);
				// printf("Before we free newPathDest after copying over file\n");
				
				//free the paths
				
				free(newPathDest);
			}
			else{
				//change srcPath

				//no not append the name to Dest, if it is a directory (will be done on fcnCall: dealWithDirectory)
				strcat(newPath, "/");//add for the directory
				// printf("newSrcPath for dir: %s\n", newPath);
				// printf("newPathDest for dir: %s\n", newDirForDest);
				dealWithDirectory(newPath, newDirForDest);
			}

			//free allocated structures
			free(newPath);
			//printf("After free, dest base is: %s\n", newDirForDest);
		}
		closedir(d);
		closedir(d_dest);
	}
	else{
		//printf("Not opening..., d=%d, d_dest=%d\n", d==NULL, d_dest==NULL);
		if (d != NULL){ 
			closedir(d);
		}
		if (d_dest != NULL){
			closedir(d_dest);
		}
	}
	//free this allocated string too -> WHAt a fucking stupid problem!
	//store all allocated structures in the loop, then de-allocate them after the loop
	free(newDirForDest);
	free(dirName);
	free(newDirForSrc);

}
void copyOverFile(char * source, char *dest){
	//copy over the file

	int fd, fd_write;
	int flag = O_RDONLY;
	//open the source file 
	fd = open(source, flag);
	//create new file with rad and write permission
	fd_write = open(dest, O_CREAT|O_RDWR|O_TRUNC, S_IWUSR|S_IRUSR);
	//check for issue with opening the file
	if (fd < 0){ 
		// syserror(open, source);
		return;
	}
	if (fd_write < 0){
		// syserror(open, dest);
		return;
	}
	char buf[4096];
	int read_bytes = 1;
	//read the src and write to dest file
	while (read_bytes != 0){
		read_bytes = read(fd, buf, 4096);
		//copy over buf to newFile
		int amount = write(fd_write, buf, read_bytes);
		//an error occurred as no bytes were written
		if (read_bytes != amount){
			// syserror(write, dest);
			return;
		}
	}
	int src_closed = close(fd);
	int dest_closed = close(fd_write);
	//returns 0 on success, else it is still open
	if (src_closed != 0){
		// syserror(close, source);
		return;
	}
	if (dest_closed != 0){
		// syserror(close, dest);
		return;
	}
	//here we are ok!
	// printf("Ok!\n");
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	//define the sources and dests
	char * source = argv[1];
	char * dest = argv[2];
	//it's a file, so copy it over
	if (isFile(source)){
		copyOverFile(source, dest);
	}
	//else we have a directory
	else{
		//add the / to the end of the dir if not there yet
		dealWithDirectory(source, dest);
	}
	// printf("Finished copying...\n");
	return 0;
}



