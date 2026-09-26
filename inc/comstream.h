/* inc/comstream.h - public client API for com-port-api */
#ifndef COMSTREAM_H
#define COMSTREAM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default pipe name that comstreamd.exe listens on. */
#define COMSTREAM_DEFAULT_PIPE  "\\\\.\\pipe\\comstream"

/* Pass this as the timeout to block forever. */
#define COMSTREAM_WAIT_INFINITE  ((unsigned)-1)

/* Opaque handle. Callers never poke inside. */
typedef struct comstream comstream_t;

/* Open a connection to a comstream daemon.
 *
 *   pipe_name: e.g. COMSTREAM_DEFAULT_PIPE, or a custom name.
 *   wait_ms:   how long to wait for the daemon to appear before giving up.
 *              Pass COMSTREAM_WAIT_INFINITE to block until it appears.
 *
 * Returns a handle on success, NULL on failure (with a message on stderr).
 */
comstream_t *comstream_open(const char *pipe_name, unsigned wait_ms);

/* Read up to len bytes into buf.
 *
 *   timeout_ms: how long to wait for data.
 *               0                       -> non-blocking, returns immediately
 *               COMSTREAM_WAIT_INFINITE -> block until at least one byte
 *               other                   -> wait up to this many ms
 *
 * Returns:
 *   > 0   number of bytes copied into buf
 *     0   timeout (no data)
 *    -1   connection lost or error (handle should be closed)
 */
int comstream_read(comstream_t *cs, void *buf, size_t len, unsigned timeout_ms);

/* Close and free the handle. Safe to call with NULL. */
void comstream_close(comstream_t *cs);

/* Return the pipe name this handle was opened with. Read-only, valid
 * until comstream_close. May return NULL if cs is NULL. */
const char *comstream_pipe_name(const comstream_t *cs);

#ifdef __cplusplus
}
#endif

#endif /* COMSTREAM_H */