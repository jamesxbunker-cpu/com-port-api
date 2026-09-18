/* src/serial.c */
#include "serial.h"
#include <stdio.h>
#include <stdlib.h>

struct serial_port {
    HANDLE handle;
    char   name[64];
};

serial_port_t *serial_open(const char *port, DWORD baud) {
    if (!port || !*port) return NULL;

    serial_port_t *sp = calloc(1, sizeof(*sp));
    if (!sp) return NULL;

    snprintf(sp->name, sizeof(sp->name), "\\\\.\\%s", port);

    sp->handle = CreateFileA(
        sp->name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        0,                      /* synchronous, same as phase 1 */
        NULL);

    if (sp->handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile(%s) failed: %lu\n",
                sp->name, GetLastError());
        free(sp);
        return NULL;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(sp->handle, &dcb)) {
        fprintf(stderr, "GetCommState failed: %lu\n", GetLastError());
        goto fail;
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

    if (!SetCommState(sp->handle, &dcb)) {
        fprintf(stderr, "SetCommState failed: %lu\n", GetLastError());
        goto fail;
    }

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout        = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant   = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 0;
    SetCommTimeouts(sp->handle, &to);

    SetupComm(sp->handle, 4096, 4096);
    PurgeComm(sp->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return sp;

fail:
    CloseHandle(sp->handle);
    free(sp);
    return NULL;
}

void serial_close(serial_port_t *sp) {
    if (!sp) return;
    if (sp->handle != INVALID_HANDLE_VALUE) CloseHandle(sp->handle);
    free(sp);
}

int serial_read(serial_port_t *sp, void *buf, size_t len, DWORD timeout_ms) {
    if (!sp || !buf || len == 0) return -1;

    /* Phase 1 used a fixed 10 ms Sleep. For step 2.1 we just do a blocking
     * read; MSVC's MAXDWORD interval timeout means it returns as soon as
     * any byte is available. */
    DWORD got = 0;
    if (!ReadFile(sp->handle, buf, (DWORD)len, &got, NULL)) {
        fprintf(stderr, "ReadFile failed: %lu\n", GetLastError());
        return -1;
    }
    if (got == 0 && timeout_ms > 0) {
        /* Nothing there right now - caller decides what to do. */
        Sleep(timeout_ms < 10 ? 10 : 10);
    }
    return (int)got;
}