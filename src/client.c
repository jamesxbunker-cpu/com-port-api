/* src/client.c - minimal named-pipe client */
#include <windows.h>
#include <stdio.h>

#define PIPE_NAME "\\\\.\\pipe\\comstream"

int main(void) {
    /* Wait up to 5 seconds for the pipe to exist. If the server isn't
     * running yet, this gives it a chance to come up. */
    if (!WaitNamedPipeA(PIPE_NAME, 5000)) {
        fprintf(stderr, "WaitNamedPipe failed: %lu\n", GetLastError());
        fprintf(stderr, "Is comstreamd.exe running?\n");
        return 1;
    }

    /* Connect. GENERIC_READ because the server is PIPE_ACCESS_OUTBOUND
     * (server writes, client reads). */
    HANDLE pipe = CreateFileA(
        PIPE_NAME,
        GENERIC_READ,
        0,                      /* no sharing */
        NULL,
        OPEN_EXISTING,          /* must already exist */
        0,                      /* sync I/O for now */
        NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile(%s) failed: %lu\n",
                PIPE_NAME, GetLastError());
        return 1;
    }

    printf("Connected to %s\n", PIPE_NAME);

    char buf[512];
    DWORD got = 0;

    for (;;) {
        BOOL ok = ReadFile(pipe, buf, sizeof(buf), &got, NULL);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                /* Server closed the pipe. Normal shutdown. */
                printf("\nServer closed the pipe.\n");
                break;
            }
            fprintf(stderr, "ReadFile failed: %lu\n", err);
            break;
        }

        if (got == 0) {
            /* No more data, but the pipe is still open. Loop. */
            continue;
        }

        /* Dump as hex + ASCII so it matches readcom.exe's output style. */
        for (DWORD i = 0; i < got; i++) printf("%02X ", (unsigned char)buf[i]);
        printf("\n");
        fflush(stdout);
    }

    CloseHandle(pipe);
    return 0;
}