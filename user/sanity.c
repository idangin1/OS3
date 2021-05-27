//#include "kernel/types.h"
//#include "kernel/stat.h"
//#include "user.h"
//#include "kernel/fs.h"
//
//
//#define PGSIZE 4096
//
//void freeOnePageTest(){
//     int procAvailablePages = getFreePagesAmount();
//   	printf( "freeOnePageTest Start: #FreePages=%d\n", procAvailablePages);
//   	sbrk(PGSIZE);
//   	int procAvailablePagesAfterSBRK = getFreePagesAmount();
//     printf( "After sbrk(PGSIZE) FreePages=%d\n", procAvailablePagesAfterSBRK);
//     if (procAvailablePagesAfterSBRK - procAvailablePages != 0)
//           printf( "freeOnePageTest Passed!!!!!!!!!\n");
//     else
//       printf( "freeOnePageTest Failed!!!!!!!!!!\n");
//   }
//
//
//void parentChildsForkTest(){
//     int pid=fork();
//     if(pid==0){
//           printf("parentChildsForkTest, child 1 before: %d \n",getFreePagesAmount());
//         }
//     else{
//             wait(0);
//             pid=fork();
//             if(pid== 0){
//                   printf("parentChildsForkTest, child 2 before: %d\n", getFreePagesAmount());
//                 }
//             else{
//                   printf("parentChildsForkTest, father is: %d\n", getFreePagesAmount());
//               }
//         }
//    }
//
//
//
//void execTest() {
//
//              // fork
//                      int pid = fork();
//      switch (pid) {
//            case -1: { // error
//                  printf("fork failed\n");
//                  return;
//                }
//                case 0: {   // child
//                  char *echo_argv[] = { "echo", "Nir", "Idan", "sanity\n", 0 };
//                  if(exec("echo", echo_argv) < 0){
//                          printf("EXEC FORK TEST FAILED!!!!!!!!! :( \n");
//                          exit(1);
//                      }
//                  break;
//                }
//                default: {  //parent
//                  wait(0);
//                  printf("exec fork test passed!!!!!\n");
//                }
//              }
//        printf("exec fork test passed!!!!!\n");
//    }
//
//
//
//int main(int argc, char *argv[]){
//      freeOnePageTest();
//      parentChildsForkTest();
//      execTest();
//      exit(1);
//
//             return 0;
//    }

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define PGSIZE 4096
#define ARR_SIZE 55000

/*
	Test used to check the swapping mechanism in fork.
*/
void nirdanForkTest(){

    char * arr = malloc (50000); //allocates 13 pages (sums to 16)
    int i = 0;
    for (i = 0; i < 50; i++) {
        arr[49100+i] = 'A'; //last six A's stored in page #16, the rest in #15
        arr[45200+i] = 'B'; //all B's are stored in page #15.
    }
    arr[49100+i] = 0; //for null terminating string...
    arr[45200+i] = 0;

    if (fork() == 0){ //is son
        for (i = 40; i < 50; i++) {
            arr[49100+i] = 'C'; //changes last ten A's to C
            arr[45200+i] = 'D'; //changes last ten B's to D
        }
        printf("SON: %s\n",&arr[49100]); // should print AAAAA..CCC...
        printf("SON: %s\n",&arr[45200]); // should print BBBBB..DDD...
        printf("\n");
        free(arr);
        exit(0);
    } else { //is parent
        wait(0);
        printf("PARENT: %s\n",&arr[49100]); // should print AAAAA...
        printf("PARENT: %s\n",&arr[45200]); // should print BBBBB...
        free(arr);
    }

}


static unsigned long int next = 1;
int getRandNum() {
    next = next * 1103515245 + 12341;
    return (unsigned int)(next/65536) % ARR_SIZE;
}

#define PAGE_NUM(addr) ((uint)(addr) & ~0xFFF)
#define TEST_POOL 500
/*
Global Test:
Allocates 17 pages (1 code, 1 space, 1 stack, 14 malloc)
Using pseudoRNG to access a single cell in the array and put a number in it.
Idea behind the algorithm:
	Space page will be swapped out sooner or later with scfifo or lap.
	Since no one calls the space page, an extra page is needed to play with swapping (hence the #17).
	We selected a single page and reduced its page calls to see if scfifo and lap will become more efficient.
Results (for TEST_POOL = 500):
LIFO: 42 Page faults
LAP: 18 Page faults
SCFIFO: 35 Page faults
*/
void globalTest(){
    char * arr;
    int i;
    int randNum;
    arr = malloc(ARR_SIZE); //allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
    for (i = 0; i < TEST_POOL; i++) {
        randNum = getRandNum();	//generates a pseudo random number between 0 and ARR_SIZE
        while (PGSIZE*10-8 < randNum && randNum < PGSIZE*10+PGSIZE/2-8)
            randNum = getRandNum(); //gives page #13 50% less chance of being selected
        //(redraw number if randNum is in the first half of page #13)
        arr[randNum] = 'X';				//write to memory
    }
    printf("finished with amount of pageFaults: %d\n",getPageFaultAmount());
    free(arr);
}


int main(int argc, char *argv[]){
    globalTest();			//for testing each policy efficiency
//    forkTest();			//for testing swapping machanism in fork.
    exit(0);
}