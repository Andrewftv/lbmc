#ifndef __MSLEEP_H__
#define __MSLEEP_H__

#include <stdint.h>

typedef void* msleep_h;

#define INFINITE_WAIT   		(-1)

#define MSLEEP_TIMEOUT          0
#define MSLEEP_INTERRUPT        1
#define MSLEEP_ERROR            (-1)

#ifdef __cplusplus
extern "C" {
#endif

int msleep_init(msleep_h *h);
void msleep_uninit(msleep_h h);
int msleep_wait(msleep_h h, int timeout);
int msleep_wakeup(msleep_h h);
int msleep_wakeup_broadcast(msleep_h h);

#ifdef __cplusplus
}
#endif

#endif

