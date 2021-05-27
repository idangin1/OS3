#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/param.h"

void page_fault_test();

void fork_test(){
    int child_pid = fork();
    if (child_pid < 0) {
        printf("fork failed\n");
    }
    else if (child_pid > 0) { // father
        printf("new child PID is: %d\n", child_pid);
        page_fault_test();
        int status;
        wait(&status);
        printf("Child PID: %d exit with status: %d\n",child_pid, status);
    } else { // child
        printf("new child created\n");
        page_fault_test();
    }
}
#define ARR_SIZE 85000
// num of page fault with ARR_SIZE 85000 is: 41
//#define ARR_SIZE 75000
// num of page fault with ARR_SIZE 75000 is: 37


void page_fault_test(){
    printf("starting page_fault_test\n");
    char * arr;
    int i;
    arr = malloc(ARR_SIZE); //allocates 14 pages (sums to 17 - to allow more then one swapping in scfifo)
    printf("malloc finished\n");
    for(i = 0; i < ARR_SIZE; i++){
        arr[i] = 'X'; // write to memory
    }
    printf("after first loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
    for(i = 0; i < ARR_SIZE; i++){
        arr[i] = 'Y'; // write to memory
    }
    printf("after second loop arr[ARR_SIZE-1] is: %c\n",arr[ARR_SIZE-1]);
    free(arr);
    printf("Num of page faults: %d \n",getPageFaultAmount());
}

int main(int argc, char *argv[]) {
    page_fault_test();
    fork_test();
    exit(0);
}