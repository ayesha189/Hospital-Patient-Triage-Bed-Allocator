/*
 * patient_simulator.c
 * Compile: gcc -Wall -o patient_simulator patient_simulator.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "hospital.h"

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: patient_simulator <id> <priority> <bed_type> <pipe_fd>\n");
        return 1;
    }

    int   id       = atoi(argv[1]);
    int   priority = atoi(argv[2]);
    char *bed_type = argv[3];
    int   pipe_fd  = atoi(argv[4]);

    char name[64] = {0};
    read(pipe_fd, name, sizeof(name) - 1);
    close(pipe_fd);

    printf("[Patient %d | %s] Arrived | Priority: %d | Bed: %s\n",
           id, name, priority, bed_type);
    fflush(stdout);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    int duration;
    if (strcmp(bed_type, "ICU") == 0) duration = 20 + rand() % 10;
    else if (strcmp(bed_type, "ISOLATION") == 0) duration = 3  + rand() % 8;
    else                                          duration = 2  + rand() % 7;

    printf("[Patient %d | %s] Treatment started | Duration: %ds\n", id, name, duration);
    fflush(stdout);

    sleep(duration);

    int fd = open(DISCHARGE_FIFO, O_WRONLY);
    if (fd >= 0) {
        char msg[16];
        int  len = snprintf(msg, sizeof(msg), "%d\n", id);
        write(fd, msg, len);
        close(fd);
    }

    printf("[Patient %d | %s] Discharged.\n", id, name);
    fflush(stdout);
    return 0;
}
