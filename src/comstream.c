/* src/comstream.c - client-side implementation of comstream */
#include "comstream.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct comstream {
    HANDLE pipe;
    char   pipe_name[256];
};

comstream_t *comstream_open(const char *pipe_name, unsigned wait_ms) {
    if (!pipe_name || !*pipe_name) {
        fprintf(stderr, "comstream_open: empty pipe name\n");
        return NULL;
    }

    /* Optionally wait for the daemon to appear. Skip if wait_ms is 0. */
    if (wait_ms > 0) {
        DWORD win_wait = (wait_ms == COMSTREAM_WAIT_INFINITE)
                             ? NMPWAIT_WAIT_FOREVER
                             : (DWORD)wait_ms;
        if (!WaitNamedPipeA(pipe_name, win_wait)) {
            fprintf(stderr, "comstream_open: WaitNamedPipe failed: %lu\n",
                    GetLastError());
            return NULL;
        }
    }

    HANDLE pipe = CreateFileA(
        pipe_name,
        GENERIC_READ,
        0,                      /* no sharing */
        NULL,
        OPEN_EXISTING,
        0,                      /* synchronous reads are fine for a client */
        NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "comstream_open: CreateFile(%s) failed: %lu\n",
                pipe_name, GetLastError());
        return NULL;
    }

    comstream_t *cs = calloc(1, sizeof(*cs));
    if (!cs) {
        CloseHandle(pipe);
        return NULL;
    }

    cs->pipe = pipe;
    strncpy(cs->pipe_name, pipe_name, sizeof(cs->pipe_name) - 1);
    cs->pipe_name[sizeof(cs->pipe_name) - 1] = '\0';
    return cs;
}

int comstream_read(comstream_t *cs, void *buf, size_t len, unsigned timeout_ms) {
    if (!cs || !buf || len == 0) return -1;

    /* Blocking read case: no polling, just wait in the kernel. */
    if (timeout_ms == COMSTREAM_WAIT_INFINITE) {
        DWORD got = 0;
        if (!ReadFile(cs->pipe, buf, (DWORD)len, &got, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                return -1;
            }
            fprintf(stderr, "comstream_read: ReadFile failed: %lu\n", err);
            return -1;
        }
        return (int)got;
    }

    /* Timed or non-blocking: poll PeekNamedPipe until data is available. */
    unsigned waited = 0;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(cs->pipe, NULL, 0, NULL, &avail, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                return -1;
            }
            fprintf(stderr, "comstream_read: PeekNamedPipe failed: %lu\n", err);
            return -1;
        }
        if (avail > 0) break;

        if (timeout_ms == 0) return 0;              /* non-blocking */
        if (waited >= timeout_ms) return 0;         /* timed out */

        Sleep(5);
        waited += 5;
    }

    /* Data is available. Cap the read so a huge len doesn't ask the pipe
     * for more than the buffer we know is there. */
    DWORD to_read = (DWORD)((len > 4096) ? 4096 : len);
    DWORD got = 0;
    if (!ReadFile(cs->pipe, buf, to_read, &got, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
            return -1;
        }
        fprintf(stderr, "comstream_read: ReadFile failed: %lu\n", err);
        return -1;
    }
    return (int)got;
}

void comstream_close(comstream_t *cs) {
    if (!cs) return;
    if (cs->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(cs->pipe);
    }
    free(cs);
}

const char *comstream_pipe_name(const comstream_t *cs) {
    return cs ? cs->pipe_name : NULL;
}