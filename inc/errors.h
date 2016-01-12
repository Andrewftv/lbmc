#ifndef __LBMC_ERRORS_H__
#define __LBMC_ERRORS_H__

typedef enum {
    L_OK = 0,
    /* Special states */
    L_TIMEOUT = 1,
    L_STOPPING = 2,
    /* Errors */
    L_FAILED = -1
} ret_code_t;

#endif

