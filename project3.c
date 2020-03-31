// project3.c
// Author: Jashwant Raj Gunasekaran @mailto jqg5480@psu.edu
// Description: Driver code for project 3 on page-replacement

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <malloc.h>
#include <sys/mman.h>
#include <unistd.h>
#include "473_mm.h"

#define MAX_OPS 1000

FILE *fd; // Input File

// Operations per line
char operation[100];
int pageNumber;
int offset;
int result;

int PAGE_SIZE;

int open_file(char *filename)
{
	char mode = 'r';
	fd = fopen(filename, &mode);
	if (fd == NULL)
	{
		printf("Invalid input file specified: %s\n", filename);
		return -1;
	}
	else
	{
		return 0;
	}
}

void close_file()
{
	fclose(fd);
}

int  read_next_ops()
{
	char line[1024];
	if(!fgets(line, 1024, fd)) {
		return 0;
	}

	char *token;
	char* rest=line;
	if(token=strtok_r(rest, " ", &rest)) {
		strcpy(operation, token);
	} else {
		return 0;
	}
	if(token=strtok_r(rest, " ", &rest)) {
		pageNumber = atoi(token);
	} else {
		return 0;
	}
	if(token=strtok_r(rest, " ", &rest)) {
		offset = atoi(token);
	} else {
		return 0;
	}
	if(token=strtok_r(rest, " ", &rest)) {
		result = atoi(token);
	} else {
		return 0;
	}
	return 1;
}

void mm_logger(int virt_page, int fault_type, int evicted_page, int write_back, unsigned int phy_addr)
{
	stats[statCounter]->virt_page = virt_page;
	stats[statCounter]->fault_type = fault_type;
	stats[statCounter]->evicted_page = evicted_page;
	stats[statCounter]->write_back = write_back;
	stats[statCounter]->phy_addr = phy_addr;
	statCounter++;
}
void print_stats()
{
	int i=0;
  printf("Cause\tvirt-page\tevicted-virt-page\twrite-back\tphy-addr\n");
	for(i=0;i<statCounter;i++) {
		printf("%d\t\t%d\t\t%d\t\t%d\t\t0x%04x\n",stats[i]->fault_type,stats[i]->virt_page,stats[i]->evicted_page,stats[i]->write_back,stats[i]->phy_addr);
	}
}


int main(int argc, char *argv[])
{
  // Sanity Check Input
	if (argc < 3) {
		printf ("Not enough parameters provided.  Usage: ./out <replacement_policy> <input_file>\n");
		printf ("  page replacement policy: 1 - FIFO\n");
		printf ("  page replacement policy: 2 - Third Chance\n");
		return -1;
	}
	if (open_file(argv[2]) < 0) {
		return -1;
	}
  int policy = (atoi(argv[1]));
  if(policy != 1 && policy != 2) {
    printf("Unknown replacement policy specified\n");
    return -1;
  }

  // Init
  int i=0;
	for(i=0;i<MAX_OPS;i++)
		stats[i] = (struct MM_stats *)malloc(sizeof(struct MM_stats));
	
	int* vm_ptr;
	PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	printf("Page Size: %d\n", PAGE_SIZE);
	int vm_size = 16*PAGE_SIZE;
	int temp;
	
  vm_ptr=memalign(PAGE_SIZE, vm_size);
	if(vm_ptr==NULL) {
		printf("FAILURE in virtual memory allocation\n");	
		return 0;
	}

  mm_init((void*)vm_ptr, vm_size, 4, PAGE_SIZE, policy);

  // Do Read/Write Operations
  while(read_next_ops()){
		if(strcmp(operation,"read")==0){
			temp = vm_ptr[offset + ((int)((pageNumber*PAGE_SIZE)/sizeof(int)))];	
		} else if(strcmp(operation,"write")==0){
			vm_ptr[offset + ((int)((pageNumber*PAGE_SIZE)/sizeof(int)))] = result;	
		} else{
			printf("Incorrect input file content\n");
			return 0;
		}
	} 
  
  print_stats();
  
  // Cleanup
  for(i=0;i<MAX_OPS;i++)
    free(stats[i]);
  close_file();
	return 0;
}
