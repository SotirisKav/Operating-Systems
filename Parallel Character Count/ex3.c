#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define p 8

int active_processes = 0;

void sigint_handler (int sig) { //typically ctrl-c signal
    printf("\nTotal number of active processes: %d\n", active_processes);
    sleep(1);
}

int main (int argc, char *argv[]) {
    int fdr, fdw;
    char buff[1024];
    ssize_t rcnt, wcnt;
    int result=0;

    signal(SIGINT, sigint_handler); //declare the signal handler

    fdr = open(argv[1], O_RDONLY);
    if (fdr == -1) {
        perror("Problem opening file to read\n");
        exit(1);
    }
    fdw = open(argv[2], O_WRONLY | O_TRUNC);
    if (fdw == -1) {
        perror("Problem opening file to write\n");
        exit(1);
    }

    off_t file_size = lseek(fdr, 0, SEEK_END);
    lseek(fdr, 0, SEEK_SET); //set pointer to start of fdr
    off_t chunk_size = file_size / p;
    off_t remainder = file_size % p;
    
    int data_pfd[p][2]; //for parent to write the file for child to read
    int result_pfd[p][2]; //for child to write result to parent
    pid_t child_pids[p]; //static declaration of pid array for the children

    for (int i = 0; i < p; i++) {
        if (pipe(data_pfd[i]) < 0) {
            perror("data pipe creation error");
            exit(1);
        }

        if (pipe(result_pfd[i]) < 0) {
            perror("result pipe creation error");
            exit(1);
        }
    }

    for (int i = 0; i < p; i++) { //for all child processes
        sleep(1); //delay 1 sec
        pid_t child_pid = fork(); //create the child process i
        if (child_pid < 0) {
            perror("fork error");
            exit(1);
        }
        else if (child_pid == 0) { //child_process i
            signal(SIGINT, SIG_IGN); //children ignore the signal handler
            for (int j = 0; j < p; j++) { //close all unused pipe ends
                if (j != i) {
                    close(data_pfd[j][0]);
                    close(data_pfd[j][1]);
                    close(result_pfd[j][0]);
                    close(result_pfd[j][1]);
                } else {
                    close(data_pfd[j][1]);  
                    close(result_pfd[j][0]);  
                }
            }
            off_t current_chunk_size = (i == p - 1) ? (chunk_size + remainder) : chunk_size;
            char *chunk_buffer = malloc(current_chunk_size); //dynamic declaration of current chunk buffer
            if (chunk_buffer == NULL) {
                perror("malloc error");
                exit(1);
            }

            rcnt = read(data_pfd[i][0], chunk_buffer, current_chunk_size); //read from parent
            if (rcnt < 0) {
                perror("read from pipe error");
                exit(1);
            }
            int count = 0; //count occurences of character
            for (int j = 0; j < rcnt; j++) {
                if (chunk_buffer[j] == argv[3][0]) {
                    count++;
                }
            }
            
            write(result_pfd[i][1], &count, sizeof(count));  //write result back to parent
            
            free(chunk_buffer); //start cleaning up
            close(data_pfd[i][0]);
            close(result_pfd[i][1]);
            exit(0);
        }
        else { //parent process
            child_pids[i] = child_pid;
            active_processes++;
        }
    }

    for (int i = 0; i < p; i++) { //parent process so close any unneeded ends
        close(data_pfd[i][0]);  
        close(result_pfd[i][1]); 
    }

    char *buffer = malloc(chunk_size + remainder);
    if (buffer == NULL) {
        perror("malloc error");
        exit(1);
    }
    for (int i = 0; i < p; i++) { //read data and pass it to data_pipes
        off_t this_chunk_size = (i == p - 1) ? (chunk_size + remainder) : chunk_size;
        rcnt = read(fdr, buffer, this_chunk_size); //parent reads the file
        if (rcnt < 0) {
            perror("read from file error");
            exit(1);
        }

        if (write(data_pfd[i][1], buffer, rcnt) < 0) { //write to data pipe
            perror("write to pipe error");
            free(buffer);
            exit(1);
        }
        close(data_pfd[i][1]);  //close pipe after sending data
    }
    free(buffer);

    for (int i = 0; i < p; i++) { //wait for all children to finish
        waitpid(child_pids[i], NULL, 0);
        active_processes--;
    }

    for (int i = 0; i < p; i++) { //collect results from children
        int partial_result;
        if (read(result_pfd[i][0], &partial_result, sizeof(partial_result)) < 0) {
            perror("read result error");
            exit(1);
        }
        result += partial_result;
        close(result_pfd[i][0]);
    }

    char output[256]; //write results to output file
    snprintf(output, sizeof(output), "The character '%c' appears %d times in file %s.\n", 
             argv[3][0], result, argv[1]);
    wcnt = write(fdw, output, strlen(output));
    if (wcnt < 0) {
        perror("write error");
        exit(1);
    }

    close(fdr); //clean up
    close(fdw);

    return 0;    
}
