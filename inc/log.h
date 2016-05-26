/*
 *      Copyright (C) 2016  Andrew Fateyev
 *      andrew.ftv@gmail.com
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __LBMC_LOG_H__
#define __LBMC_LOG_H__

typedef enum {
    LOG_LVL_VERB,
    LOG_LVL_INFO,
    LOG_LVL_WARN,
    LOG_LVL_ERROR,
    LOG_LVL_ALERT,
    LOG_LVL_FATAL
} log_dbg_lvl_t;

void logs_init(char *file);
void logs_uninit(void);
void lbmc_log(const char *file, int line, log_dbg_lvl_t lvl, char *fmt, ...);

#define DBG_V(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_VERB, fmt, ##__VA_ARGS__); \
    } while(0)
#define DBG_I(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_INFO, fmt, ##__VA_ARGS__); \
    } while(0)
#define DBG_W(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_WARN, fmt, ##__VA_ARGS__); \
    } while(0)
#define DBG_E(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_ERROR, fmt, ##__VA_ARGS__); \
    } while(0)
#define DBG_A(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_ALERT, fmt, ##__VA_ARGS__); \
    } while(0)
#define DBG_F(fmt,...) \
    do { \
    lbmc_log(__FILE__, __LINE__, LOG_LVL_FATAL, fmt, ##__VA_ARGS__); \
    } while(0)

#endif
