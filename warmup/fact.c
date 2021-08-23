#include "common.h"
#include <stdbool.h>
int 
factCalc(int n){

	if (n == 1){
		return 1;
	}
	return n*factCalc(n-1);
}
bool hasDecimal(char * str){
	int i = 0;
	for (i = 0; str[i] != '\0'; i++){
		if (str[i] == '.'){
			return true;
		}
	}
	return false;
}
int
main(int argc, char ** argv)
{
	if (argc == 1 || argc > 2){
		printf("Huh?\n");
	}
	else if (atoi(argv[1]) <= 0){
		printf("Huh?\n");
	} 
	//check for floater
	else if (hasDecimal(argv[1])){
		// itoa(num, snum, (atoi(argv[1])))
		printf("Huh?\n");
	}

	else if(atoi(argv[1]) > 12){
		printf("Overflow\n");
	}
	else{
		printf("%d\n", factCalc(atoi(argv[1])));

	}
	return 0;
}
