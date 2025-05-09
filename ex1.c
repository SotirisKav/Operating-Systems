#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char buff[1024];
    int count = 0;
    int fdr, fdw;
    size_t len;
    ssize_t rcnt, wcnt;
    fdr = open(argv[1], O_RDONLY); //returns file to be read desriptor
    if (fdr == -1) {
        perror("Problem opening file to read\n");
        exit(1);
    }
    fdw = open(argv[2], O_WRONLY| O_TRUNC); //returns file to be written into descriptor & write only & removes any existing content
    if (fdw == -1) {
        perror("Problem opening file to write\n");
        exit(1);
    }

    for (;;) {
        rcnt = read(fdr, buff, sizeof(buff) - 1); //store the data to the buffer
        if (rcnt == 0) //EOF 
            break;
        if (rcnt == -1) { 
            perror("read");
            exit(1);
        }
        buff[rcnt] = '\0'; //make the last character null terminator
        len = strlen(buff);
        int i = 0;
        while(i < len) {
            if(buff[i++] == argv[3][0]) {
                count++;
            }
        }
    }
    char output[256];
    snprintf(output, sizeof(output), "The character '%c' appears %d times in file %s.\n", argv[3][0], count, argv[1]); //formats *safely* a message to output buffer
    write(fdw, output, strlen(output)); 
    close(fdr);
    close(fdw);
    return 0;
}			
