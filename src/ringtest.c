/* src/ringtest.c - temporary test for ringbuf */
#include "ringbuf.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    ringbuf_t *rb = ringbuf_create(16);   /* small on purpose */

    /* Test 1: basic write + read */
    const char *msg1 = "hello";
    ringbuf_write(rb, msg1, 5);

    size_t cursor = 0;
    char out[32] = {0};
    int overran = 0;
    size_t n = ringbuf_read(rb, &cursor, out, sizeof(out), &overran);
    printf("Test 1: read %zu bytes: '%s' overran=%d\n", n, out, overran);
    /* expect: read 5 bytes: 'hello' overran=0 */

    /* Test 2: wrap-around - write 14 bytes into a 16-byte buffer */
    ringbuf_write(rb, "abcdefghijklmn", 14);
    cursor = 0;
    memset(out, 0, sizeof(out));
    n = ringbuf_read(rb, &cursor, out, sizeof(out), &overran);
    printf("Test 2: read %zu bytes: '%s' overran=%d\n", n, out, overran);
    /* expect: read 14 bytes: 'abcdefghijklmn' overran=0 */

    /* Test 3: overrun - write more than capacity */
    ringbuf_write(rb, "0123456789ABCDEFGHIJ", 20);  /* 20 into 16-byte buffer */
    cursor = 0;
    memset(out, 0, sizeof(out));
    n = ringbuf_read(rb, &cursor, out, sizeof(out), &overran);
    out[n] = 0;
    printf("Test 3: read %zu bytes: '%s' overran=%d\n", n, out, overran);
    /* expect: 15 bytes (capacity-1), the newest 15 of the 20 written */

    /* Test 4: two independent cursors */
    cursor = 0;
    size_t cursor2 = 0;
    memset(out, 0, sizeof(out));
    n = ringbuf_read(rb, &cursor, out, 5, &overran);
    printf("Test 4a: cursor1 read %zu bytes: '%.*s'\n", n, (int)n, out);
    memset(out, 0, sizeof(out));
    n = ringbuf_read(rb, &cursor2, out, sizeof(out), &overran);
    printf("Test 4b: cursor2 read %zu bytes: '%.*s'\n", n, (int)n, out);
    /* cursor1 starts at head (nothing new), cursor2 gets everything */

    ringbuf_destroy(rb);
    return 0;
}