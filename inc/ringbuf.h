/* inc/ringbuf.h */

/* inc/ringbuf.h */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>

typedef struct ringbuf ringbuf_t;

ringbuf_t *ringbuf_create(size_t capacity);
void       ringbuf_destroy(ringbuf_t *rb);

/* Write len bytes. Never fails - overwrites oldest data if needed.
 * Returns bytes written (always == len). */
size_t ringbuf_write(ringbuf_t *rb, const void *src, size_t len);

/* Snapshot the current write position. A reader uses this as its
 * starting cursor. */
size_t ringbuf_head(ringbuf_t *rb);

/* Copy up to len bytes from the buffer starting at *cursor into dst.
 * Updates *cursor to point just past the bytes copied.
 * Returns bytes copied (may be 0 if the cursor caught up to head).
 *
 * If the cursor falls behind tail (writer overwrote unread data),
 * the cursor is auto-advanced to tail and a flag is returned so the
 * caller knows it lost data. */
size_t ringbuf_read(ringbuf_t *rb, size_t *cursor,
                    void *dst, size_t len, int *overran);

/* Diagnostic: how many bytes are currently available from the oldest
 * unread position to head. */
size_t ringbuf_used(ringbuf_t *rb);

#endif
