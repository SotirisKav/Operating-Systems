#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char **argv){
    pid_t pid = fork();
    if (pid < 0 ) {
        perror("fork");
        exit(1);
    }
    else if (pid == 0){
        char *arg_child[] = {"/home/oslab/oslab004/lab1/ex1", argv[1], argv[2], argv[3], NULL};
        if (execv(arg_child[0], arg_child) < 0){
            perror("execv");
            exit(1);
        }
    }
    else {
        int status;
        wait(&status);
    }
    return 0;

}    
