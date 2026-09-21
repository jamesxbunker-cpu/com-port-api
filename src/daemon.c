/* src/daemon.c - minimal named-pipe server */
#include <windows.h>
#include <stdio.h>

#define PIPE_NAME "\\\\.\\pipe\\comstream"

int main(void) {
    printf("Creating pipe %s ...\n", PIPE_NAME);

    /* Create one pipe instance. PIPE_ACCESS_OUTBOUND = server only writes.
     * PIPE_TYPE_BYTE = raw bytes, no message framing. PIPE_WAIT = blocking. */
    HANDLE pipe = CreateNamedPipeA(
        PIPE_NAME,
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,                      /* max instances */
        4096,                   /* out buffer */
        4096,                   /* in buffer (unused for outbound) */
        0,                      /* default timeout */
        NULL);                  /* default security */

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateNamedPipe failed: %lu\n", GetLastError());
        return 1;
    }

    printf("Waiting for a client to connect ...\n");

    /* Block until a client connects. */
    BOOL connected = ConnectNamedPipe(pipe, NULL);
    if (!connected) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            /* ERROR_PIPE_CONNECTED is fine - client beat us here. Anything
             * else is a real failure. */
            fprintf(stderr, "ConnectNamedPipe failed: %lu\n", err);
            CloseHandle(pipe);
            return 1;
        }
    }

    printf("Client connected. Sending greeting.\n");

    const char *msg = "hello\n";
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, msg, (DWORD)strlen(msg), &written, NULL);
    if (!ok) {
        fprintf(stderr, "WriteFile failed: %lu\n", GetLastError());
    } else {
        printf("Sent %lu bytes.\n", written);
    }

    /* Clean shutdown. FlushFileBuffers blocks until the client has read
     * everything we wrote - important, otherwise CloseHandle can discard
     * buffered data. */
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    printf("Done.\n");
    return 0;
}