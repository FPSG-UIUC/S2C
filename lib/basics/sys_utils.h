#ifndef __SYS_UTILS_H__
#define __SYS_UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

void pin_p_core(int core_id);

FILE* open_and_check(char* filename, const char* mode);

FILE* open_database_file(int hpo);

#endif
