#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PG_SIZE 4096
#define MAX_PG_NUM 27

void sanity_test(){
        char *pages = malloc(PG_SIZE * MAX_PG_NUM);
        printf("Allocated %d pages\n", MAX_PG_NUM);

                for (int i = 0; i < MAX_PG_NUM; i++){
                printf("write to page %d: %d\n", i, i);
                pages[i * PG_SIZE] = i;
            }

                for (int i = 0; i < MAX_PG_NUM; i++){
                printf("read from page %d: %d\n", i, pages[i * PG_SIZE]);
            }
        free(pages);
    }

void NFUA_LAPA_tests(){
        char *pages = malloc(PG_SIZE * 17);
        for (int i = 0; i < 16; i++){
                pages[i * PG_SIZE] = i;
            }
        sleep(2); // update age
        for (int i = 0; i < 15; i++){
                pages[i * PG_SIZE] = i;
            }
        sleep(2); // update age
        pages[16 * PG_SIZE] = 16; // should replace page #15 - check kernel print
    }

void SCFIFO_test(){
        char *pages = malloc(PG_SIZE * 18);
        for (int i = 0; i < 16; i++){
                pages[i * PG_SIZE] = i;
            }
        // RAM: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 15
                pages[16 * PG_SIZE] = 16;
        // RAM: 16 1 2 3 4 5 6 7 8 9 10 11 12 13 15
                pages[1 * PG_SIZE] = 1;
        pages[17 * PG_SIZE] = 17; // should replace page #2 - check kernel print
    }

void NONE_test(){
        char *pages = malloc(PG_SIZE * 17);
        for (int i = 0; i < 17; i++){
                pages[i * PG_SIZE] = i;
            }
        printf("pages[16 * PG_SIZE] = %d\n", pages[16 * PG_SIZE]); // should not be 16
    }

void fork_test(){
        char *pages = malloc(PG_SIZE * 17);
        for (int i = 0; i < 17; i++){
                pages[i * PG_SIZE] = i;
            }
        for (int i = 0; i < 17; i++){
                printf("pages[%d * PG_SIZE] = %d\n", i, pages[i * PG_SIZE]);
            }
        printf("###FORKING###\n");
        int pid = fork();
        if(pid == 0){
                printf("###CHILD###\n");
                for (int i = 0; i < 17; i++){
                        printf("pages[%d * PG_SIZE] = %d\n", i, pages[i * PG_SIZE]);
                    }
            }
        else{
                int status;
                wait(&status);
            }
    }

void exec_test(){
        char *pages = malloc(PG_SIZE * 17);
        for (int i = 0; i < 17; i++){
                pages[i * PG_SIZE] = i;
            }
        printf("exec output: %d\n", exec("exec_fail", 0)); // hope exec will fail and return -1
        printf("pages[10 * PG_SIZE] = %d\n", pages[10 * PG_SIZE]); // should print 10
    }

int main()
{
    printf("hello test_task3\n");
//    sanity_test();
    NFUA_LAPA_tests();
//     SCFIFO_test();
    // NONE_test();
//    fork_test();
//    exec_test();
    exit(0);
}