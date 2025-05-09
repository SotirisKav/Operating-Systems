#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int current_process = 0; //for long text files multiple forks may be needed

int child(char *buffer, char *search){
    int count=0, i=0;
    while (buffer[i] != '\0'){
        if (buffer[i] == *search)
            count++;
        i++;
    }
    return count;
}

int main(int argc, char **argv){
    char buff[1024];
    int fdr, fdw, rcnt, wcnt;
    int count=0;
    fdr=open(argv[1], O_RDONLY);
    if (fdr == -1) {
        perror("open");
        exit(1);
    }
    fdw=open(argv[2], O_WRONLY | O_TRUNC);
    if (fdw == -1) {
        perror("open");
        exit(1);
    }
    for (;;){
        int result=0;
        rcnt=read(fdr, buff, sizeof(buff)-1);
        if (rcnt == 0)
            break;
        if (rcnt == -1){
            perror("read");
            return 1;
        }
        buff[rcnt] = '\0';
        pid_t p, mypid;
        int pfd[2];
        if (pipe(pfd) < 0){
            perror("pipe");
            exit(1);
        }
        mypid = -1;
        current_process++;
        p = fork();
        if (p < 0){
            perror("fork");
            exit(1);
        }
        else if (p == 0){
            close(pfd[0]);
            mypid = getpid();
            pid_t parent = getppid();
            printf("Hello World from the child process #%d! My ID is: %d, and my parent's ID is: %d\n", current_process, mypid, parent);
            result=child(buff, &argv[3][0]);
            if (write(pfd[1], &result, sizeof(result)) != sizeof(result)){
                perror("write failed");
            }
            close(pfd[1]);
            exit(0);
        }
        else {
            close(pfd[1]);
            int child_count;
            if (read(pfd[0], &child_count, sizeof(child_count)) != sizeof(child_count)){
                perror("read from pipe");
            }
            printf("Hello World from the parent process #%d! My child's ID is: %d\n", current_process, p);
            count+=child_count;
            pid_t status;
            wait(&status);
        }
    }
    int length = snprintf(buff, sizeof(buff), "%d", count);
    wcnt=write(fdw, buff, length);
    if (wcnt == -1){
        perror("write");
        return 1;
    }
    close(fdr);
    close(fdw);
    return 0;
}          
