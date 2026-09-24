/* src/daemon.c - Step 3.4: serial -> ring buffer -> single pipe client */
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

/* ---- signal handler ---- */
static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_running, 0);
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
        return 1;
    }

    fprintf(stderr, "[daemon] waiting for client on %s\n", PIPE_NAME);

    /* Async ConnectNamedPipe with an event, so we can poll g_running. */
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        fprintf(stderr, "[daemon] CreateEvent failed: %lu\n", GetLastError());
        CloseHandle(pipe);
        return 1;
    }

    BOOL connected = ConnectNamedPipe(pipe, &ov);
    if (!connected) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            /* Wait for connection, but wake up regularly to check g_running. */
            while (InterlockedCompareExchange(&g_running, 1, 1)) {
                DWORD w = WaitForSingleObject(ov.hEvent, 200);
                if (w == WAIT_OBJECT_0) break;
                /* WAIT_TIMEOUT: loop, check g_running, try again. */
            }
            if (!InterlockedCompareExchange(&g_running, 1, 1)) {
                /* Shutting down mid-wait. Cancel the pending connect. */
                CancelIo(pipe);
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                fprintf(stderr, "[daemon] pipe thread exiting (no client ever connected)\n");
                return 0;
            }
        } else if (err != ERROR_PIPE_CONNECTED) {
            fprintf(stderr, "[daemon] ConnectNamedPipe failed: %lu\n", err);
            CloseHandle(ov.hEvent);
            CloseHandle(pipe);
            return 1;
        }
    }

    CloseHandle(ov.hEvent);
    fprintf(stderr, "[daemon] client connected\n");
    /* ... rest of the streaming loop unchanged ... */
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

    /* Start the serial reader thread. */
    HANDLE th_serial = CreateThread(NULL, 0, serial_thread, NULL, 0, NULL);
    if (!th_serial) {
        fprintf(stderr, "CreateThread(serial) failed: %lu\n", GetLastError());
        return 1;
    }

    /* Run the pipe loop on the main thread. It exits when a client
     * disconnects or Ctrl+C is pressed. */
    pipe_thread(NULL);

    /* Wait for the serial thread to notice g_running==0 and exit. */
    WaitForSingleObject(th_serial, 2000);

    serial_close(g_serial);
    ringbuf_destroy(g_ring);
    CloseHandle(th_serial);

    fprintf(stderr, "[daemon] shutdown complete\n");
    return 0;
}