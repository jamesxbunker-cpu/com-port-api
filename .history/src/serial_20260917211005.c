/* src/serial.c */
#include "serial.h"
#include <stdio.h>
#include <stdlib.h>

struct serial_port {
    HANDLE handle;
    OVERLAPPED read_ov; // Async reads
    HANDLE read_event; // signal for when read is finished
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
        FILE_FLAG_OVERLAPPED,                      /* asynchronous */
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
        if (sp->handle && sp->handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(sp->handle);
        }
        if (sp->read_event) {
            CloseHandle(sp->read_event);
        }
        free(sp);
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

    // OS signal when read event finishes
    // manual reset so only cleared after seen by program
    sp->read_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!sp->read_event) {
        fprintf(stderr, "CreateEvent failed: %lu\n", GetLastError());
        if (sp->handle && sp->handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(sp->handle);
        }
        if (sp->read_event) {
            CloseHandle(sp->read_event);
        }
        free(sp);
        return NULL;
    }
        
    sp->read_ov.hEvent = sp->read_event;

    PurgeComm(sp->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return sp;
}

void serial_close(serial_port_t *sp) {
    if (!sp) return;
    if (sp->handle != INVALID_HANDLE_VALUE){
        CancelIo(sp->handle); // cancel pending read
        CloseHandle(sp->handle);
    }
    if (sp->read_event){
        CloseHandle(sp->read_event);
    }
    free(sp);
}

int serial_read(serial_port_t *sp, void *buf, size_t len, DWORD timeout_ms) {
    if (!sp || !buf || len == 0) {
        return -1;
    }

    ResetEvent(sp->read_event); // clear event before new read command issued

    /* Previous used a fixed 10 ms Sleep. For now we just do a blocking
     * read; MSVC's MAXDWORD interval timeout means it returns as soon as
     * any byte is available. */
    DWORD got = 0;
BOOL ok = ReadFile(sp->handle, buf, (DWORD)len, &got, &sp->read_ov);

    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            /* Real failure, not just async-in-progress. */
            fprintf(stderr, "ReadFile failed: %lu\n", err);
            return -1;
        }
        /* Read is pending. Wait for the event, up to timeout_ms. */
        DWORD w = WaitForSingleObject(sp->read_event, timeout_ms);
        if (w == WAIT_TIMEOUT) {
            /* Give up on this read; cancel it so the next call starts clean. */
            CancelIo(sp->handle);
            return 0;
        }
        if (w != WAIT_OBJECT_0) {
            fprintf(stderr, "WaitForSingleObject failed: %lu\n", GetLastError());
            return -1;
        }
        /* Event fired: collect the result. */
        if (!GetOverlappedResult(sp->handle, &sp->read_ov, &got, FALSE)) {
            DWORD e = GetLastError();
            if (e == ERROR_OPERATION_ABORTED) return 0;   /* we cancelled it */
            fprintf(stderr, "GetOverlappedResult failed: %lu\n", e);
            return -1;
        }
    }
    return (int)got;
}