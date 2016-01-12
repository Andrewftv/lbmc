#ifndef __TIME_UTILS_H__
#define __TIME_UTILS_H__

#include <time.h>
#include <stdint.h>

#include "errors.h"

/*
 * Compare two timespec structures
 * Return -1 if t1 less t2, 1 if t1 above t2 and 0 if equal.
 */
int util_time_compare(struct timespec *t1, struct timespec *t2);
/*
 * Substruct time.
 * Return time difference in miliseconds.
 */
int util_time_sub(struct timespec *t1, struct timespec *t2);
ret_code_t util_time_add(struct timespec *t, uint32_t ms);

#endif

