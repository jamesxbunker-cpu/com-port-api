/* src/ringbuf.c */
#include "ringbuf.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct ringbuf {
    unsigned char *data;
    size_t         capacity;
    size_t         head;       /* next write position */
    size_t         tail;       /* oldest unread position */
    CRITICAL_SECTION lock;     /* Windows mutex, ready for 3.4 */
};

ringbuf_t *ringbuf_create(size_t capacity) {
    if (capacity == 0) return NULL;

    ringbuf_t *rb = calloc(1, sizeof(*rb));
    if (!rb) return NULL;

    rb->data = malloc(capacity);
    if (!rb->data) { free(rb); return NULL; }

    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    InitializeCriticalSection(&rb->lock);
    return rb;
}

void ringbuf_destroy(ringbuf_t *rb) {
    if (!rb) return;
    DeleteCriticalSection(&rb->lock);
    free(rb->data);
    free(rb);
}

/* Internal: how many bytes are between tail and head? */
static size_t used_unlocked(const ringbuf_t *rb) {
    if (rb->head >= rb->tail) return rb->head - rb->tail;
    return rb->capacity - rb->tail + rb->head;
}

size_t ringbuf_write(ringbuf_t *rb, const void *src, size_t len) {
    if (!rb || !src || len == 0) return 0;

    const unsigned char *p = src;
    EnterCriticalSection(&rb->lock);

    /* If this write alone is bigger than the whole buffer, keep only the
     * tail end of it - the rest would be overwritten anyway. */
    if (len >= rb->capacity) {
        p += (len - rb->capacity);
        len = rb->capacity - 1;    /* leave one byte gap so head!=tail means nonempty */
    }

    /* If adding len would collide with tail, advance tail. This is the
     * "overwrite oldest data" policy. */
    size_t used = used_unlocked(rb);
    if (used + len >= rb->capacity) {
        size_t need_to_drop = used + len - (rb->capacity - 1);
        rb->tail = (rb->tail + need_to_drop) % rb->capacity;
    }

    /* Write, wrapping if necessary. */
    size_t first = rb->capacity - rb->head;
    if (first > len) first = len;
    memcpy(rb->data + rb->head, p, first);
    if (len > first) {
        memcpy(rb->data, p + first, len - first);
    }
    rb->head = (rb->head + len) % rb->capacity;

    LeaveCriticalSection(&rb->lock);
    return len;
}

size_t ringbuf_head(ringbuf_t *rb) {
    if (!rb) return 0;
    EnterCriticalSection(&rb->lock);
    size_t h = rb->head;
    LeaveCriticalSection(&rb->lock);
    return h;
}

size_t ringbuf_read(ringbuf_t *rb, size_t *cursor,
                    void *dst, size_t len, int *overran) {
    if (!rb || !cursor || !dst || len == 0) return 0;

    EnterCriticalSection(&rb->lock);

    *overran = 0;

    /* If our cursor is behind tail, the writer lapped us. Jump to tail. */
    size_t used = used_unlocked(rb);
    size_t cursor_offset;
    if (*cursor >= rb->tail) {
        cursor_offset = *cursor - rb->tail;
    } else {
        cursor_offset = rb->capacity - rb->tail + *cursor;
    }
    if (cursor_offset > used) {
        *cursor = rb->tail;
        *overran = 1;
        cursor_offset = 0;
    }

    /* How many bytes can we actually copy? */
    size_t avail = used - cursor_offset;
    if (avail > len) avail = len;
    if (avail == 0) {
        LeaveCriticalSection(&rb->lock);
        return 0;
    }

    size_t start = (*cursor) % rb->capacity;
    size_t first = rb->capacity - start;
    if (first > avail) first = avail;
    memcpy(dst, rb->data + start, first);
    if (avail > first) {
        memcpy((unsigned char *)dst + first, rb->data, avail - first);
    }

    *cursor = (*cursor + avail) % rb->capacity;

    LeaveCriticalSection(&rb->lock);
    return avail;
}

size_t ringbuf_used(ringbuf_t *rb) {
    if (!rb) return 0;
    EnterCriticalSection(&rb->lock);
    size_t u = used_unlocked(rb);
    LeaveCriticalSection(&rb->lock);
    return u;
}