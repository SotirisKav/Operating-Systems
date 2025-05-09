#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

int dispatcher_to_worker_pfd[2];
int worker_to_dispatcher_pfd[2];
volatile sig_atomic_t should_exit = 0; //variable can change at any time
bool task_completed;

typedef struct {
    off_t start_pos;
    off_t chunk_size;
} WorkMessage;

void work(const char* file, char target, WorkMessage work_msg, int dispatcher_to_worker_pfd[2], int worker_to_dispatcher_pfd[2]) {
    off_t start_pos = work_msg.start_pos;
    off_t offset = work_msg.chunk_size;

    int fdr = open(file, O_RDONLY);
    if (fdr < 0) {
        perror("worker failed to open file");
        exit(1);
    }

    if (lseek(fdr, start_pos, SEEK_SET) < 0) { //move to start position
        perror("Worker failed to seek in file");
        close(fdr);
        exit(1);
    }

    char* buffer = malloc(offset);
    if (buffer == NULL) {
        perror("Worker: Memory allocation error");
        close(fdr);
        exit(1);
    }

    int bytes_read = read(fdr, buffer, offset);
    if (bytes_read < 0) {
        perror("worker failed to read file");
        free(buffer);
        close(fdr);
        exit(1);
    }
    int count = 0; //count occurences of target character
    for (int i = 0; i < bytes_read; i++) {
        if (buffer[i] == target) {
            count++;
        }
        if (should_exit) {
            printf("Worker: Termination requested during processing\n");
            break;
        }
    }

    sleep(1); // puts a small delay after work is done
    if (write(worker_to_dispatcher_pfd[1], &count, sizeof(count)) < 0) {
        perror("Worker failed to write to dispatcher");
    }
    free(buffer);
    close(fdr);
}

void term_handler(int sig) {
    should_exit = 1;
}

int main(int argc, char* argv[]) {
    signal(SIGTERM, term_handler);
    off_t start_pos = atoll(argv[1]); //to long long if file is very long
    off_t offset = atoll(argv[2]);
    const char* file = argv[3];
    char target = argv[4][0];
    dispatcher_to_worker_pfd[0] = atoi(argv[5]);
    worker_to_dispatcher_pfd[1] = atoi(argv[6]);
    task_completed = (argv[7][0] == '1');
    close(dispatcher_to_worker_pfd[1]); //worker does not write to this pipe
    close(worker_to_dispatcher_pfd[0]); //worker does not read from this pipe

    WorkMessage work_msg;
    while (!task_completed) {
        if (read(dispatcher_to_worker_pfd[0], &work_msg, sizeof(work_msg)) <= 0) {
             exit(1);
        }
        if (work_msg.chunk_size < 10){
            task_completed = true;
        }
        work(file, target, work_msg, dispatcher_to_worker_pfd, worker_to_dispatcher_pfd);
        
    }
    close(dispatcher_to_worker_pfd[0]);
    close(worker_to_dispatcher_pfd[1]);
    return 0;
}


