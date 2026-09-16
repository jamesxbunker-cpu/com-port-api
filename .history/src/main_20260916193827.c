/* main.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG g_running = 1;

static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_running, 0);
        return TRUE;
    }
    return FALSE;
}

static HANDLE open_com(const char *port, DWORD baud) {
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);

    HANDLE h = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        0,                      /* exclusive - no sharing */
        NULL,
        OPEN_EXISTING,
        0,                      /* synchronous */
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile(%s) failed: %lu\n",
                path, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        fprintf(stderr, "GetCommState failed: %lu\n", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = dcb.fInX = FALSE;

    if (!SetCommState(h, &dcb)) {
        fprintf(stderr, "SetCommState failed: %lu\n", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Timeouts: read returns as soon as ANY byte is available. */
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout        = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant   = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 0;
    SetCommTimeouts(h, &to);

    SetupComm(h, 4096, 4096);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return h;
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

    unsigned char buf[512];
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