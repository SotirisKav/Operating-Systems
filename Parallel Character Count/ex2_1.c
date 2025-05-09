#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int x = 1;

int main(int argc, char **argv){
    pid_t p, mypid;
    mypid = -1;
    p = fork();
    if (p < 0){
        perror("fork");
        exit(1);
    }
    else if (p == 0){ //we are inside the child process
        printf("hello world\n");
        mypid = getpid();
        printf("Child id is: %d\n", mypid);
        pid_t parent = getppid();
        printf("Parent id is: %d\n", parent);
        x = 10;
        printf("x for Child is: %d\n", x); 
        exit(0);
    }
    else {
        printf("Child id from parent is: %d\n", p);
        printf("x for Parent is: %d\n", x);
        pid_t status;
        wait(&status); //parent waits for the child to finish: otherwise we might end up with a ZOMBIE process
        printf("Child died\n");
    }
    return 0;
}          
