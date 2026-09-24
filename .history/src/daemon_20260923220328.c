/* src/daemon.c - serial -> ring buffer -> single pipe client */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "serial.h"
#include "ringbuf.h"

#define PIPE_NAME   "\\\\.\\pipe\\comstream"
#define RING_SIZE   (1024 * 1024)   /* 1 MB of history */

/* ---- shared state between the two threads ---- */
static serial_port_t *g_serial;
static ringbuf_t     *g_ring;
static volatile LONG  g_running = 1;
static HANDLE         g_listen_pipe = INVALID_HANDLE_VALUE;

/* ---- signal handler ---- */
static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_running, 0);
        if (g_listen_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(g_listen_pipe);
            g_listen_pipe = INVALID_HANDLE_VALUE;
        }
        return TRUE;
    }
    return FALSE;
}

/* ---- Thread A: read from serial, push into ring ---- */
static DWORD WINAPI serial_thread(LPVOID arg) {
    (void)arg;
    unsigned char buf[1024];

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int n = serial_read(g_serial, buf, sizeof(buf), 200);
        if (n < 0) {
            fprintf(stderr, "[daemon] serial_read failed, exiting reader\n");
            InterlockedExchange(&g_running, 0);
            break;
        }
        if (n == 0) continue;

        ringbuf_write(g_ring, buf, (size_t)n);
        /* Optional: log throughput. Omit to stay quiet. */
    }
    fprintf(stderr, "[daemon] serial thread exiting\n");
    return 0;
}

/* ---- Create one pipe instance in listening state ---- */
static HANDLE create_pipe_instance(void) {
    return CreateNamedPipeA(
        PIPE_NAME,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,                          /* max instances (still 1 for 3.4) */
        64 * 1024,                  /* out buffer */
        0,                          /* in buffer */
        0, NULL);
}

/* ---- Thread B: accept one client, stream from ring ---- */
static DWORD WINAPI pipe_thread(LPVOID arg) {
    (void)arg;

    HANDLE pipe = create_pipe_instance();
    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[daemon] CreateNamedPipe failed: %lu\n", GetLastError());
        InterlockedExchange(&g_running, 0);
        return 1;
    }

    fprintf(stderr, "[daemon] waiting for client on %s\n", PIPE_NAME);

    BOOL connected = ConnectNamedPipe(pipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        fprintf(stderr, "[daemon] ConnectNamedPipe failed: %lu\n", GetLastError());
        CloseHandle(pipe);
        InterlockedExchange(&g_running, 0);
        return 1;
    }

    fprintf(stderr, "[daemon] client connected\n");

    /* This client's read cursor. Start at head so it only sees fresh bytes;
     * change to 0 to replay the current buffer contents on connect. */
    size_t cursor = ringbuf_head(g_ring);

    unsigned char chunk[4096];

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int overran = 0;
        size_t n = ringbuf_read(g_ring, &cursor, chunk, sizeof(chunk), &overran);
        if (overran) {
            fprintf(stderr, "[daemon] client fell behind, dropped bytes\n");
        }
        if (n == 0) {
            /* Nothing available - sleep briefly. This is the one place a
             * poll is acceptable because we're gated by client throughput
             * and pipe writes, not by the serial driver. */
            Sleep(5);
            continue;
        }

        /* Write the whole chunk to the pipe. Partial writes go to the
         * remaining bytes; a failed write means the client went away. */
        size_t sent = 0;
        while (sent < n) {
            DWORD wrote = 0;
            BOOL ok = WriteFile(pipe, chunk + sent, (DWORD)(n - sent),
                                &wrote, NULL);
            if (!ok) {
                DWORD err = GetLastError();
                fprintf(stderr, "[daemon] WriteFile failed: %lu\n", err);
                /* Client gone. Break out and wait for another? For 3.4,
                 * we just exit. 3.6 will loop back to accept again. */
                InterlockedExchange(&g_running, 0);
                goto done;
            }
            sent += wrote;
        }
    }

done:
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    fprintf(stderr, "[daemon] pipe thread exiting\n");
    return 0;
}

/* ---- main ---- */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s COM3 [baud]\n", argv[0]);
        return 1;
    }
    DWORD baud = (argc > 2) ? (DWORD)strtoul(argv[2], NULL, 10) : 115200;

    SetConsoleCtrlHandler(on_ctrl, TRUE);

    g_serial = serial_open(argv[1], baud);
    if (!g_serial) return 1;

    g_ring = ringbuf_create(RING_SIZE);
    if (!g_ring) {
        fprintf(stderr, "ringbuf_create failed\n");
        serial_close(g_serial);
        return 1;
    }

    fprintf(stderr, "[daemon] serial open on %s @ %lu\n", argv[1], baud);

    HANDLE th_serial = CreateThread(NULL, 0, serial_thread, NULL, 0, NULL);
    if (!th_serial) {
        fprintf(stderr, "CreateThread(serial) failed: %lu\n", GetLastError());
        return 1;
    }

    HANDLE th_pipe = CreateThread(NULL, 0, pipe_thread, NULL, 0, NULL);
    if (!th_pipe) {
        fprintf(stderr, "CreateThread(pipe) failed: %lu\n", GetLastError());
        return 1;
    }

    /* Wait for both threads to exit. The Ctrl+C handler sets g_running=0
     * and closes g_listen_pipe, which unblocks the pipe thread. */
    WaitForSingleObject(th_pipe,   5000);
    WaitForSingleObject(th_serial, 5000);

    serial_close(g_serial);
    ringbuf_destroy(g_ring);
    CloseHandle(th_serial);
    CloseHandle(th_pipe);

    fprintf(stderr, "[daemon] shutdown complete\n");
    return 0;
}