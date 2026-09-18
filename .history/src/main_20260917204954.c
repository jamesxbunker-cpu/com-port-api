/* main.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "serial.h"

static volatile LONG g_running = 1;

static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_running, 0);
        return TRUE;
    }
    return FALSE;
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: readcom COM3 [baud]\n");
        return 1;
    }
    DWORD baud = (argc > 2) ? (DWORD)atoi(argv[2]) : 115200;

    SetConsoleCtrlHandler(on_ctrl, TRUE);

    HANDLE h = open_com(argv[1], baud);
    if (h == INVALID_HANDLE_VALUE) return 1;

    printf("Reading from %s at %lu baud. Ctrl+C to quit.\n",
           argv[1], baud);

    unsigned char buf[1024];
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf), &got, NULL)) {
            fprintf(stderr, "ReadFile failed: %lu\n", GetLastError());
            break;
        }
        if (got == 0) {
            Sleep(10);
            continue;
        }
        /* Dump raw bytes as hex + ASCII so we can see what's really there */
        for (DWORD i = 0; i < got; i++) {
            printf("%02X ", buf[i]);
            if ((i + 1) % 16 == 0) {
                printf("\n");
            }
        }
        fflush(stdout);
    }

    CloseHandle(h);
    printf("\nClosed.\n");
    return 0;
}