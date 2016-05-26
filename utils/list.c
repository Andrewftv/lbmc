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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "list.h"

typedef struct {
    list_node_t *head;
    pthread_mutex_t mutex;
    int count;
} list_t;

/* Private part */

static void add_head_priv(list_t *ctx, list_node_t *node)
{
    node->prev = NULL;
    node->next = ctx->head;
    if (ctx->head)
        ctx->head->prev = node;
    ctx->head = node;
    ctx->count++;
}

static list_node_t *find_tail_priv(list_t *ctx)
{
    list_node_t *tail = ctx->head;

    tail = ctx->head;
    while (tail->next)
    tail = tail->next;

    return tail;
}

static void add_tail_priv(list_t *ctx, list_node_t *node)
{
    list_node_t *tail = find_tail_priv(ctx);

    node->next = NULL;
    tail->next = node;
    node->prev = tail;
    ctx->count++;
}

static list_node_t *remove_head_priv(list_t *ctx)
{
    list_node_t *ret_node;

    ret_node = ctx->head;
    ctx->head = ctx->head->next;
    ctx->count--;

    return ret_node;
}

static list_node_t *remove_tail_priv(list_t *ctx)
{
    list_node_t *tail = find_tail_priv(ctx), *ret_node;

    ret_node = tail;
    if (tail == ctx->head)
        ctx->head = NULL;
    else
        tail->prev->next = NULL;
    ctx->count--;

    return ret_node;
}

/* Constructor and destructor */

int slist_init(list_h *h)
{
    list_t *ctx;

    ctx = (list_t *)malloc(sizeof(list_t));
    if (!ctx)
        return -1;

    memset(ctx, 0, sizeof(list_t));
    pthread_mutex_init(&ctx->mutex, NULL);
    *h = ctx;

    return 0;
}

void slist_uninit(list_h h)
{
    list_t *ctx = (list_t *)h;

    if (!ctx)
        return;

    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
}

/* Thread safe functions */

int slist_add_head(list_h h, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    pthread_mutex_lock(&ctx->mutex);
    add_head_priv(ctx, node);
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

int slist_add_tail(list_h h, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    pthread_mutex_lock(&ctx->mutex);
    if (!ctx->head)
    {
        add_head_priv(ctx, node);
        goto Exit;
    }
    add_tail_priv(ctx, node);
Exit:
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

list_node_t *slist_get_remove_head(list_h h)
{
    list_t *ctx = (list_t *)h;
    list_node_t *ret_node;

    if (!ctx || !ctx->head)
        return NULL;

    pthread_mutex_lock(&ctx->mutex);
    ret_node = remove_head_priv(ctx);
    pthread_mutex_unlock(&ctx->mutex);

    ret_node->prev = NULL;
    ret_node->next = NULL;
    return ret_node;
}

list_node_t *slist_get_remove_tail(list_h h)
{
    list_t *ctx = (list_t *)h;
    list_node_t *ret_node;

    if (!ctx || !ctx->head)
        return NULL;

    pthread_mutex_lock(&ctx->mutex);
    ret_node = remove_tail_priv(ctx);
    pthread_mutex_unlock(&ctx->mutex);

    ret_node->prev = NULL;
    ret_node->next = NULL;
    return ret_node;
}

list_node_t *slist_find_remove(list_h h, find_func func, void *user_data)
{
    list_t *ctx = (list_t *)h;
    list_node_t *tmp;

    if (!func)
        return NULL;

    pthread_mutex_lock(&ctx->mutex);
    tmp = ctx->head;
    while (tmp)
    {
        if (func(tmp, user_data))
            break;

        tmp = tmp->next;
    }
    if (!tmp)
        goto Exit;

    if (tmp == ctx->head)
    {
        remove_head_priv(ctx);
    }
    else if (!tmp->next)
    {
        remove_tail_priv(ctx);
    }
    else
    {
        tmp->prev->next = tmp->next;
        tmp->next->prev = tmp->prev;
        ctx->count--;
    }

    tmp->prev = NULL;
    tmp->next = NULL;
Exit:
    pthread_mutex_unlock(&ctx->mutex);

    return tmp;
}

int slist_get_count(list_h h)
{
    list_t *ctx = (list_t *)h;
    int count = 0;

    pthread_mutex_lock(&ctx->mutex);
    if (ctx)
        count = ctx->count;
    pthread_mutex_unlock(&ctx->mutex);

    return count;
}

/* Not thread safe functions */

int list_add_head(list_h h, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    add_head_priv(ctx, node);

    return 0;
}

int list_add_tail(list_h h, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    if (!ctx->head)
    {
        add_head_priv(ctx, node);
        goto Exit;
    }
    add_tail_priv(ctx, node);
Exit:

    return 0;
}

int list_insert_after(list_h h, list_node_t *after, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    if (!after || !after->next)
    {
        add_tail_priv(ctx, node);
        goto Exit;
    }
    after->next->prev = node;
    node->next = after->next;
    node->prev = after;
    after->next = node;
    ctx->count++;
Exit:

    return 0;
}

int list_insert_before(list_h h, list_node_t *before, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    if (!before || !before->prev)
    {
        add_head_priv(ctx, node);
        goto Exit;
    }
    before->prev->next = node;
    node->prev = before->prev;
    node->next = before;
    before->prev = node;
    ctx->count++;
Exit:

    return 0;
}

list_node_t *list_get_first(list_h h)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !ctx->head)
        return NULL;

    return ctx->head;
}

list_node_t *list_get_tail(list_h h)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !ctx->head)
        return NULL;

    return find_tail_priv(ctx);
}

list_node_t *list_get_next(list_node_t *node)
{
    if (!node)
        return NULL;

    return node->next;
}

list_node_t *list_get_priv(list_node_t *node)
{
    if (!node)
        return NULL;

    return node->prev;
}

list_node_t *list_remove_head(list_h h)
{
    list_t *ctx = (list_t *)h;
    list_node_t *ret_node;

    if (!ctx || !ctx->head)
        return NULL;

    ret_node = remove_head_priv(ctx);

    ret_node->prev = NULL;
    ret_node->next = NULL;
    return ret_node;
}

list_node_t *list_remove_tail(list_h h)
{
    list_t *ctx = (list_t *)h;
    list_node_t *ret_node;

    if (!ctx || !ctx->head)
        return NULL;

    ret_node = remove_tail_priv(ctx);

    ret_node->prev = NULL;
    ret_node->next = NULL;
    return ret_node;
}

int list_remove(list_h h, list_node_t *node)
{
    list_t *ctx = (list_t *)h;

    if (!ctx || !node)
        return -1;

    if (node == ctx->head)
    {
        remove_head_priv(ctx);
    }
    else if (!node->next)
    {
        remove_tail_priv(ctx);
    }
    else
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        ctx->count--;
    }

    node->prev = NULL;
    node->next = NULL;

    return 0;
}

list_node_t *list_find(list_h h, find_func func, void *user_data)
{
    list_t *ctx = (list_t *)h;
    list_node_t *tmp = NULL;

    if (!func)
    return NULL;

    tmp = ctx->head;
    while (tmp)
    {
        if (func(tmp, user_data))
            break;

        tmp = tmp->next;
    }

    return tmp;
}

void list_lock(list_h h)
{
    list_t *ctx = (list_t *)h;

    pthread_mutex_lock(&ctx->mutex);
}

void list_unlock(list_h h)
{
    list_t *ctx = (list_t *)h;

    pthread_mutex_unlock(&ctx->mutex);
}

int list_get_count(list_h h)
{
    list_t *ctx = (list_t *)h;
    int count = 0;

    if (ctx)
        count = ctx->count;

    return count;
}

