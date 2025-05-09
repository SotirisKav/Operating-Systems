#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#define MAX_WORKERS 36

int frontend_to_dispatcher_pfd[2];
int dispatcher_to_frontend_pfd[2];
int active_workers = 0, completed_chunks = 0, total_chunks = 0, chunk_size = 10;
int total_chars_found = 0;
int fd_read, fd_write, fd_final_message;

typedef struct {
    pid_t pid;
    int worker_to_dispatcher_pfd[2];
    int dispatcher_to_worker_pfd[2];
    off_t start_pos, offset;
    int chunk_id; //which chunk each worker is working on: -1 if unassinged
    bool active;
} Worker;

typedef struct {
    off_t start_pos;
    off_t chunk_size;
    bool assigned;
    bool completed;
    int worker_id;  //id of the worker that has been assigned the chunk
} WorkChunk;

typedef struct {
    off_t start_pos;
    off_t chunk_size;
} WorkMessage;

Worker workers[MAX_WORKERS]; //declare array of workers statically
WorkChunk* workchunk = NULL; //declare array of chunks dynamically to handle large text files
char* file; //file name and target character
char target;
void initialise_workers() {
    for (int i = 0; i < MAX_WORKERS; i++) {
        workers[i].active = false;
        workers[i].chunk_id = -1;
        workers[i].start_pos = 0; 
        workers[i].offset = 0;
    }
}

void add_worker() {
    char message[50];
    if (active_workers >= MAX_WORKERS) {
        printf("Maximum number of workers reached\n");
        snprintf(message, sizeof(message), "Maximum number of workers reached\n");
        if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
            perror("Error writing in to pipe from dispatcher to frontend");
        }
        return;
    }

    int worker_id = -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!workers[i].active) {
            worker_id = i; //activate worker i
            break;
        }
               }

               if (worker_id == -1) { //every worker is already active
                   printf("No available workers\n");
                   return;
               }

               if (pipe(workers[worker_id].dispatcher_to_worker_pfd) < 0) { //create pipe for worker-dispatcher
                   perror("Pipe creation failed");
                   return;
               }

               if (pipe(workers[worker_id].worker_to_dispatcher_pfd) < 0) { //create pipe for worker-dispatcher
                   perror("Pipe creation failed");
                   close(workers[worker_id].dispatcher_to_worker_pfd[0]);
                   close(workers[worker_id].dispatcher_to_worker_pfd[1]);
                   return;
               }

               pid_t pid = fork(); //create new worker
               if (pid < 0) {
                   perror("Fork failed");
                   close(workers[worker_id].dispatcher_to_worker_pfd[0]);
                   close(workers[worker_id].dispatcher_to_worker_pfd[1]);
                   close(workers[worker_id].worker_to_dispatcher_pfd[0]);
                   close(workers[worker_id].worker_to_dispatcher_pfd[1]);
                   snprintf(message, sizeof(message), "Failed to create worker\n");
                     if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
                          perror("Error writing in to pipe from dispatcher to frontend");
                     }
                   return;
               }
               else if (pid == 0) {
                   close(workers[worker_id].dispatcher_to_worker_pfd[1]); //close write end of dispatcher-to-worker pipe
                   close(workers[worker_id].worker_to_dispatcher_pfd[0]); //close read end of worker-to-dispatcher pipe
                   char start_pos_str[20], chunk_size_str[20], worker_id_str[20], target_str[2];
                   char  d_to_w_pfd0_str[20], w_to_d_pfd1_str[20];
                   bool task_completed = false;
                   char task_completed_str[2];
                   char file_copy[256];
                   strncpy(file_copy, file, sizeof(file_copy) - 1); 
                   file_copy[sizeof(file_copy) - 1] = '\0';    
                   snprintf(start_pos_str, sizeof(start_pos_str), "%ld", workers[worker_id].start_pos);
                   snprintf(chunk_size_str, sizeof(chunk_size_str), "%ld", workers[worker_id].offset);
                   snprintf(worker_id_str, sizeof(worker_id_str), "%d", worker_id);
                   snprintf(target_str, sizeof(target_str), "%c", target);
                   snprintf(d_to_w_pfd0_str, sizeof(d_to_w_pfd0_str), "%d", workers[worker_id].dispatcher_to_worker_pfd[0]);
                   snprintf(w_to_d_pfd1_str, sizeof(w_to_d_pfd1_str), "%d", workers[worker_id].worker_to_dispatcher_pfd[1]);
                   snprintf(task_completed_str, sizeof(task_completed_str), "%d", task_completed);
                   char* args[] = { "./worker", start_pos_str, chunk_size_str, file_copy, target_str, d_to_w_pfd0_str, w_to_d_pfd1_str, task_completed_str, NULL };
                   execv(args[0], args);
                   perror("Exec failed");
                   exit(1);
               }
               else {
                   close(workers[worker_id].dispatcher_to_worker_pfd[0]); // Close read end of dispatcher-to-worker pipe
                   close(workers[worker_id].worker_to_dispatcher_pfd[1]); // Close write end of worker-to-dispatcher pipe
                   workers[worker_id].pid = pid;
                   workers[worker_id].active = true;
                   active_workers++;
                   snprintf(message, sizeof(message), "Worker %d added successfully\n", worker_id);
                   if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
                        perror("Error writing in to pipe from dispatcher to frontend");
                    }
               }
               return;
           }

bool distribute_work() {
    if (completed_chunks == total_chunks) { //all work has been completed
        return false;
    }
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i].active && (workers[i].chunk_id == -1)) { //worker i is available
            for (int j = 0; j < total_chunks; j++) {
                if (!workchunk[j].assigned && !workchunk[j].completed) { //chunk j is available
                    workchunk[j].assigned = true;
                    workchunk[j].worker_id = i;
                    workers[i].chunk_id = j;
                    workers[i].start_pos = workchunk[j].start_pos;
                    workers[i].offset = workchunk[j].chunk_size;

                    WorkMessage work_msg;
                    work_msg.start_pos = workchunk[j].start_pos;
                    work_msg.chunk_size = workchunk[j].chunk_size;
                    if (write(workers[i].dispatcher_to_worker_pfd[1], &work_msg, sizeof(work_msg)) < 0) {
                        perror("Failed to send work details to worker");
                        break;
                    }
                    return true; //stop after assigning one chunk
                }
            }
        }
    }
    return false;
}

void remove_worker() {
    char message[50];
    if (active_workers == 0) {
        snprintf(message, sizeof(message), "There are no active workers to remove\n");
        if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
            perror("Error writing in to pipe from dispatcher to frontend");
        }
        return;
    }
    int worker_id = -1;
    for (int i = active_workers-1; i >= 0; i--) { //Last Added First Removed
        if (workers[i].active) {
            worker_id = i;
            break;
        }
    }

    if (worker_id != -1) {
        int chunk_id = workers[worker_id].chunk_id;
        if (kill(workers[worker_id].pid, SIGTERM) < 0) {
            perror("Process kill failed");
            snprintf(message, sizeof(message), "Failed to terminate worker %d", worker_id);
            if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
                perror("Error writing to frontend pipe");
            }
            return;
        }
        close(workers[worker_id].dispatcher_to_worker_pfd[0]);
        close(workers[worker_id].dispatcher_to_worker_pfd[1]);
        close(workers[worker_id].worker_to_dispatcher_pfd[0]);
        close(workers[worker_id].worker_to_dispatcher_pfd[1]);
        workers[worker_id].active = false;
        workers[worker_id].pid = 0;
        workers[worker_id].chunk_id = -1;
        active_workers--;
        if (chunk_id != -1 && workchunk[chunk_id].assigned) {
            workchunk[chunk_id].assigned = false;
            workchunk[chunk_id].worker_id = -1;
        }
    }
    snprintf(message, sizeof(message), "Worker %d removed successfully\n", worker_id);
    if (write(dispatcher_to_frontend_pfd[1], message, strlen(message)) < 0) {
        perror("Error writing in to pipe from dispatcher to frontend");
    }
    return;
}

void create_work_chunks(const char* file) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file");
        exit(1);
    }
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        perror("Failed to determine file size");
        exit(1);
    }
    lseek(fd, 0, SEEK_SET); // move to start of file
    total_chunks = file_size / chunk_size;
    if (file_size % chunk_size != 0) {
        total_chunks++; // account for any remaining bytes
    }
    close(fd);
    workchunk = malloc(total_chunks * sizeof(WorkChunk));
    for (int i = 0; i < total_chunks; i++) {
        workchunk[i].start_pos = i * chunk_size;
        workchunk[i].chunk_size = (i == total_chunks - 1) ? (file_size % chunk_size) : chunk_size;
        workchunk[i].assigned = false;
        workchunk[i].completed = false;
        workchunk[i].worker_id = -1;
    }
    return;
}

void collect_results() {
    /*for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i].active && workers[i].chunk_id != -1) {
            int count;
            if (read(workers[i].worker_to_dispatcher_pfd[0], &count, sizeof(count)) > 0) {
                total_chars_found += count;
                workchunk[workers[i].chunk_id].completed = true;
                completed_chunks++;
                workers[i].chunk_id = -1;
            }
            else {
                perror("Failed to read results from worker");
            }
        }
    }*/
    char final_message[100];
    snprintf(final_message, sizeof(final_message),
            "COMPLETED: All work completed. Total characters found: %d\n", total_chars_found);
    if (write(fd_final_message, final_message, strlen(final_message))<0){
        perror("Failed to write to dispatcher");
    }
    pid_t frontend_pid = getppid();
    kill(frontend_pid, SIGUSR2);
}

void progress_info() {
    int completed = 0;
    for (int i = 0; i < total_chunks; i++) {
        if (workchunk[i].completed) {
            completed++;
        }
    }
    float progress_percentage = (total_chunks > 0) ? ((float)completed / total_chunks) * 100 : 0;

    char progress_message[256]; //message to be sent to frontend
    snprintf(progress_message, sizeof(progress_message),
        "Progress: %.2f%% completed (%d/%d chunks), Total Characters Found: %d\n",
        progress_percentage, completed, total_chunks, total_chars_found);

    if (write(dispatcher_to_frontend_pfd[1], progress_message, strlen(progress_message)) < 0) { //send progress info to frontend
        perror("Failed to write progress info to pipe");
    }
}

void workers_info() {
    char workers_message[256];
    snprintf(workers_message, sizeof(workers_message), "Active Workers: %d\n", active_workers);
    if (write(dispatcher_to_frontend_pfd[1], workers_message, strlen(workers_message)) < 0) {
        perror("Failed to write workers info to pipe");
    }
}

void signal_handler(int sig) {
    char code_sent[1];
    read(frontend_to_dispatcher_pfd[0], &code_sent, sizeof(code_sent));
    int code = atoi(code_sent);
    printf("\n");
    switch (code) {
    case 1:
        progress_info();
        break;
    case 2:
        add_worker();
        break;
    case 3:
        remove_worker();
        break;
    case 4:
        workers_info();
        break;
    default:
        break;
    }
}

int main(int argc, char* argv[]) {
    file = argv[1];
    fd_final_message = atoi(argv[8]);
    //fd_read = atoi(argv[1]);
    //output_file = argv[2];
    target = argv[3][0];
    frontend_to_dispatcher_pfd[0] = atoi(argv[4]);
    frontend_to_dispatcher_pfd[1] = atoi(argv[5]);
    dispatcher_to_frontend_pfd[0] = atoi(argv[6]);
    dispatcher_to_frontend_pfd[1] = atoi(argv[7]);
    initialise_workers();
    create_work_chunks(file);
    if (access("./worker", X_OK) != 0) {
        perror("Worker executable not found or not executable");
        exit(1);
    }
    signal(SIGUSR1, signal_handler);
    while (completed_chunks < total_chunks) {
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (workers[i].active && workers[i].chunk_id != -1) {
                int flags = fcntl(workers[i].worker_to_dispatcher_pfd[0], F_GETFL, 0); //retrieve current file status flags
                fcntl(workers[i].worker_to_dispatcher_pfd[0], F_SETFL, flags | O_NONBLOCK); //set the file descriptor to non-blocking & any previous flags
                int count;
                int read_result = read(workers[i].worker_to_dispatcher_pfd[0], &count, sizeof(count));

                if (read_result > 0) {
                    total_chars_found += count;
                    workchunk[workers[i].chunk_id].completed = true;
                    completed_chunks++;
                    workers[i].chunk_id = -1;

                }
                else if (read_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { //checks if the error is not due to the non-blocking nature of the pipe 
                    perror("Error reading from worker");
                }
                fcntl(workers[i].worker_to_dispatcher_pfd[0], F_SETFL, flags); //restore original flags
            }
        }

        if (active_workers > 0) {
            distribute_work();
        }
        usleep(50000);
    }
    collect_results();
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (workers[i].active) {
            kill(workers[i].pid, SIGTERM);
            close(workers[i].dispatcher_to_worker_pfd[1]);
            close(workers[i].worker_to_dispatcher_pfd[0]);
        }
    }
    close(frontend_to_dispatcher_pfd[0]);
    close(dispatcher_to_frontend_pfd[1]);
    free(workchunk);
    return 0;
}        
