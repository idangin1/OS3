#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"


#define PGSIZE 4096

void freeOnePageTest(){
     int procAvailablePages = getFreePagesAmount();
   	printf( "freeOnePageTest Start: #FreePages=%d\n", procAvailablePages);
   	sbrk(PGSIZE);
   	int procAvailablePagesAfterSBRK = getFreePagesAmount();
     printf( "After sbrk(PGSIZE) FreePages=%d\n", procAvailablePagesAfterSBRK);
     if (procAvailablePagesAfterSBRK - procAvailablePages != 0)
           printf( "freeOnePageTest Passed!!!!!!!!!\n");
     else
       printf( "freeOnePageTest Failed!!!!!!!!!!\n");
   }


void parentChildsForkTest(){
     int pid=fork();
     if(pid==0){
           printf("parentChildsForkTest, child 1 before: %d \n",getFreePagesAmount());
         }
     else{
             wait(0);
             pid=fork();
             if(pid== 0){
                   printf("parentChildsForkTest, child 2 before: %d\n", getFreePagesAmount());
                 }
             else{
                   printf("parentChildsForkTest, father is: %d\n", getFreePagesAmount());
               }
         }
    }



void execTest() {

              // fork
                      int pid = fork();
      switch (pid) {
            case -1: { // error
                  printf("fork failed\n");
                  return;
                }
                case 0: {   // child
                  char *echo_argv[] = { "echo", "Nir", "Idan", "sanity\n", 0 };
                  if(exec("echo", echo_argv) < 0){
                          printf("EXEC FORK TEST FAILED!!!!!!!!! :( \n");
                          exit(1);
                      }
                  break;
                }
                default: {  //parent
                  wait(0);
                  printf("exec fork test passed!!!!!\n");
                }
              }
        printf("exec fork test passed!!!!!\n");
    }



int main(int argc, char *argv[]){
      freeOnePageTest();
      parentChildsForkTest();
      execTest();
      exit(1);

             return 0;
    }