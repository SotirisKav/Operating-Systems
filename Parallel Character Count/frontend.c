#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>


int fd_frontend_to_dispatcher[2]; // For frontend to write to dispatcher
int fd_dispatcher_to_frontend[2]; // For frontend to read from dispatcher
int fd_final_message[2]; //For final message from dispatcher to frontend
pid_t dispatcher_pid;
int fdw;

int valid_command(char *x)
{
    if (strcmp(x, "progress") == 0)
    {
        return 1;
    }
    else if (strcmp(x, "add_worker") == 0)
    {
        return 2;
    }
    else if (strcmp(x, "remove_worker") == 0)
    {
        return 3;
    }
    else if (strcmp(x, "workers") == 0)
    {
        return 4;
    }
    return 0;
}

void signal_handler(int sig)
{   
    if (sig != SIGUSR2)
    {
        return;
    }
    char message[256];
    ssize_t bytes_read = read(fd_final_message[0], message, sizeof(message) - 1);
    if (bytes_read < 0)
    {
        perror("Failed to read final message from dispatcher");
        exit(1);
    }
    message[bytes_read] = '\0';
    if (write(fdw, message, strlen(message)) < 0){
        perror("failed to write resulti"); 
    }
    printf("%s\n", message);
    printf("All work has been completed. Exiting program.\n");
    kill(dispatcher_pid, SIGTERM);
    close(fd_frontend_to_dispatcher[1]);
    close(fd_dispatcher_to_frontend[0]);
    close(fd_final_message[0]);
    close(fdw);
    exit(0);
}


int main(int argc, char **argv){
    fdw = open(argv[2], O_WRONLY| O_TRUNC);
    if (fdw < 0) {
        perror("Problem opening file to write\n");
        exit(1);
    }
    if (pipe(fd_dispatcher_to_frontend) < 0)
    {
        perror("Read Pipe failed");
        exit(1);
    }
    if (pipe(fd_frontend_to_dispatcher) < 0)
    {
        perror("Write Pipe failed");
        exit(1);
    }
    if (pipe(fd_final_message)<0){
        perror("final message pipe failed");
        exit(1);
    }

    dispatcher_pid = fork();
    if (dispatcher_pid < 0)
    {
        perror("Fork failed");
        exit(1);
    }

    else if (dispatcher_pid == 0)
    {
        char fd_read_str[10], fd_write_str[10], pipe_read_str[10], pipe_write_str[10], parent_pid[10], final_message[10];
        snprintf(fd_read_str, sizeof(fd_read_str), "%d", fd_frontend_to_dispatcher[0]);
        snprintf(fd_write_str, sizeof(fd_write_str), "%d", fd_frontend_to_dispatcher[1]);
        snprintf(pipe_read_str, sizeof(pipe_read_str), "%d", fd_dispatcher_to_frontend[0]);
        snprintf(pipe_write_str, sizeof(pipe_write_str), "%d", fd_dispatcher_to_frontend[1]);
        snprintf(final_message, sizeof(final_message), "%d", fd_final_message[1]);
        char *args[] = {"./dispatcher", argv[1], argv[2], argv[3], fd_read_str, fd_write_str, pipe_read_str, pipe_write_str,final_message, NULL};
        if (execv(args[0], args) < 0)
        {
            perror("Execv failed");
            exit(1);
        }
    }

    printf("Available commands:\n");
    printf("1. progress to show progress of the search\n");
    printf("2. add_worker to add a new worker\n");
    printf("3. remove_worker to remove a worker\n");
    printf("4. workers to show the number of active workers\n");

    char command[16];
    signal(SIGUSR2, signal_handler);
    for (;;)
    {
        printf("Enter a new command: ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove the newline character

        int code = valid_command(command);
        if (code == 0)
        {
            printf("Invalid command\n");
            continue;
        }

        char buff[2]; // Buffer to write the command to the dispatcher
        snprintf(buff, sizeof(buff), "%d", code);
        if (write(fd_frontend_to_dispatcher[1], buff, strlen(buff)) < 0)
        { // +1 to include null terminator
            perror("Failed to write to dispatcher");
            continue;
        }
        kill(dispatcher_pid, SIGUSR1);

        char message[256];
        ssize_t bytes_read = read(fd_dispatcher_to_frontend[0], message, sizeof(message) - 1);
        if (bytes_read < 0)
        {
            perror("Failed to read from dispatcher");
            continue;
        }
        else if (bytes_read == 0)
        {
            printf("Dispatcher closed the pipe. Exiting.\n");
            break;
        }
        message[bytes_read] = '\0'; // Null-terminate the message
        printf("%s\n", message);
    }

    return 0;
}


