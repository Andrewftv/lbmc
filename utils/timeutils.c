#include "timeutils.h"

ret_code_t util_time_compare(struct timespec *t1, struct timespec *t2)
{
    if (t1->tv_sec > t2->tv_sec)
        return 1;
    else if (t1->tv_sec < t2->tv_sec)
        return -1;
    else if (t1->tv_nsec > t2->tv_nsec)
        return 1;
    else if (t1->tv_nsec < t2->tv_nsec)
        return -1;

    return L_OK;
}

int util_time_sub(struct timespec *t1, struct timespec *t2)
{
    int diff;

    diff = ((t1->tv_sec - t2->tv_sec) * 1000);
    diff += ((t1->tv_nsec - t2->tv_nsec) / 1000000);

    return diff;
}

ret_code_t util_time_add(struct timespec *t, uint32_t ms)
{
    int64_t ms_only = ms % 1000;

    t->tv_sec += ms / 1000;
    t->tv_sec += ((ms_only * 1000000 + t->tv_nsec) / 1000000000);
    t->tv_nsec += ((ms_only * 1000000 + t->tv_nsec) % 1000000000);
    if (t->tv_nsec > 999999999)
        t->tv_nsec -= 1000000000;

    return L_OK;
}

