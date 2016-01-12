#ifndef __UTILS_LISTS_H__
#define __UTILS_LISTS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void* list_h;

typedef struct list_node_s {
    struct list_node_s *prev;
    struct list_node_s *next;
} list_node_t;

typedef int (*find_func)(list_node_t *node, void *user_data);

int slist_init(list_h *h);
void slist_uninit(list_h h);

/* Thread safe functions */
int slist_add_head(list_h h, list_node_t *node);
int slist_add_tail(list_h h, list_node_t *node);

list_node_t *slist_get_remove_head(list_h h);
list_node_t *slist_get_remove_tail(list_h h);
list_node_t *slist_find_remove(list_h h, find_func func, void *user_data);

int slist_get_count(list_h h);

/* Not thread safe functions */
void list_lock(list_h h);
void list_unlock(list_h h);

list_node_t *list_get_first(list_h h);
list_node_t *list_get_tail(list_h h);
list_node_t *list_get_next(list_node_t *node);
list_node_t *list_get_priv(list_node_t *node);

int list_add_head(list_h h, list_node_t *node);
int list_add_tail(list_h h, list_node_t *node);
int list_insert_after(list_h h, list_node_t *after, list_node_t *node);
int list_insert_before(list_h h, list_node_t *before, list_node_t *node);

list_node_t *list_remove_head(list_h h);
list_node_t *list_remove_tail(list_h h);
int list_remove(list_h h, list_node_t *node);

list_node_t *list_find(list_h h, find_func func, void *user_data);

int list_get_count(list_h h);

#ifdef __cplusplus
}
#endif

#endif

