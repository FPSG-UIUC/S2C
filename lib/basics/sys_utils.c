#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include "sys_utils.h"

#ifdef __linux__
#include <sched.h>
#endif

/*#ifndef database_dir*/
/*#error "ERROR: database_dir must be defined!""*/
/*#endif*/

void pin_p_core(int core_id) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

#ifdef __linux__
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core_id, &mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
        perror("sched_setaffinity");
        exit(0);
    }
#endif
}

FILE* open_and_check(char* filename, const char* mode) {
    FILE* fp = fopen(filename, mode);
    if (fp == NULL) {
        fprintf(stderr, "ERROR: cannot open %s\n", filename);
        exit(0);
    }

    return fp;
}

FILE* open_database_file(int hpo) {
    char path[100] = {0};
    char* envvar = "LPSP_ROOT";

    if (!getenv(envvar)) {
        fprintf(stderr, "The environment variable %s was not found.\n", envvar);
        fprintf(stderr, "Did you do source ./configure?\n");
        exit(1);
    }

    if (snprintf(path, 100, "%s", getenv(envvar)) >= 100) {
        fprintf(stderr, "buffer to store %s is too small. Abort.\n", envvar);
        exit(1);
    }

    strcat(path, "/database/");

    char filename[10] = {0};
    sprintf(filename, "%d", hpo);
    strcat(path, filename);

    FILE* fp = open_and_check(path, "r");
    return fp;
}
