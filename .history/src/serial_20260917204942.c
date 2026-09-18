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