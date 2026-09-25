/* src/daemon.c - serial -> ring buffer -> multi-client pipe server */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "serial.h"
#include "ringbuf.h"

#define PIPE_NAME    "\\\\.\\pipe\\comstream"
#define RING_SIZE    (1024 * 1024)   /* 1 MB of history */
#define MAX_CLIENTS  64

/* ---- shared state ---- */
static serial_port_t *g_serial;
static ringbuf_t     *g_ring;
static volatile LONG  g_running     = 1;
static HANDLE         g_listen_pipe = INVALID_HANDLE_VALUE;

/* ---- Ctrl+C handler ---- */
static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_running, 0);
        if (g_listen_pipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(g_listen_pipe, NULL);   /*  unblocks ConnectNamedPipe */
            CloseHandle(g_listen_pipe);
            g_listen_pipe = INVALID_HANDLE_VALUE;
        }
        return TRUE;
    }
    return FALSE;
}

/* ---- Serial reader thread ---- */
static DWORD WINAPI serial_thread(LPVOID arg) {
    (void)arg;
    unsigned char buf[1024];

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int n = serial_read(g_serial, buf, sizeof(buf), 50);
        if (n < 0) {
            fprintf(stderr, "[daemon] serial_read failed, exiting reader\n");
            InterlockedExchange(&g_running, 0);
            break;
        }
        if (n == 0) continue;

        ringbuf_write(g_ring, buf, (size_t)n);
    }
    fprintf(stderr, "[daemon] serial thread exiting\n");
    return 0;
}

/* ---- Create one pipe instance, ready to accept ---- */
static HANDLE create_pipe_instance(void) {
    return CreateNamedPipeA(
        PIPE_NAME,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        MAX_CLIENTS,
        64 * 1024,
        0,
        0, NULL);
}

/* ---- Per-client worker: streams ring -> this client's pipe ---- */
static DWORD WINAPI client_thread(LPVOID arg) {
    HANDLE pipe = (HANDLE)arg;
    size_t cursor = ringbuf_head(g_ring);
    unsigned char chunk[4096];

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int overran = 0;
        size_t n = ringbuf_read(g_ring, &cursor, chunk, sizeof(chunk), &overran);
        if (overran) {
            fprintf(stderr, "[daemon] client fell behind, dropped bytes\n");
        }
        if (n == 0) {
            Sleep(5);
            continue;
        }

        size_t sent = 0;
        int failed = 0;
        while (sent < n) {
            DWORD wrote = 0;
            if (!WriteFile(pipe, chunk + sent, (DWORD)(n - sent), &wrote, NULL)) {
                fprintf(stderr, "[daemon] client write failed: %lu\n", GetLastError());
                failed = 1;
                break;
            }
            sent += wrote;
        }
        if (failed) break;
    }

    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    fprintf(stderr, "[daemon] client thread exiting\n");
    return 0;
}

/* ---- Accept loop ---- */
static DWORD WINAPI accept_thread(LPVOID arg) {
    (void)arg;

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        HANDLE pipe = create_pipe_instance();
        if (pipe == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "[daemon] CreateNamedPipe failed: %lu\n", GetLastError());
            InterlockedExchange(&g_running, 0);
            return 1;
        }

        g_listen_pipe = pipe;
        fprintf(stderr, "[daemon] waiting for client...\n");

        BOOL connected = ConnectNamedPipe(pipe, NULL);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED || err == ERROR_INVALID_HANDLE) {
                fprintf(stderr, "[daemon] accept thread exiting (shutdown)\n");
                CloseHandle(pipe);
                g_listen_pipe = INVALID_HANDLE_VALUE;
                return 0;
            }
            fprintf(stderr, "[daemon] ConnectNamedPipe failed: %lu\n", err);
            CloseHandle(pipe);
            g_listen_pipe = INVALID_HANDLE_VALUE;
            continue;
        }

        g_listen_pipe = INVALID_HANDLE_VALUE;
        fprintf(stderr, "[daemon] client connected\n");

        HANDLE th = CreateThread(NULL, 0, client_thread, pipe, 0, NULL);
        if (!th) {
            fprintf(stderr, "[daemon] CreateThread(client) failed: %lu\n", GetLastError());
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
        }
        CloseHandle(th);   /* detached - thread cleans itself up */
    }

    fprintf(stderr, "[daemon] accept thread exiting\n");
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

    HANDLE th_accept = CreateThread(NULL, 0, accept_thread, NULL, 0, NULL);
    if (!th_accept) {
        fprintf(stderr, "CreateThread(accept) failed: %lu\n", GetLastError());
        return 1;
    }

    /* Wait for the accept thread (it exits on Ctrl+C). Then wait forever
     * for the serial thread so we never close its handle underneath it. */
    WaitForSingleObject(th_accept, INFINITE);
    WaitForSingleObject(th_serial, INFINITE);

    serial_close(g_serial);
    ringbuf_destroy(g_ring);
    CloseHandle(th_serial);
    CloseHandle(th_accept);

    fprintf(stderr, "[daemon] shutdown complete\n");
    return 0;
}