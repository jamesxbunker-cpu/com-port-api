/* src/main.c */
#include "serial.h"
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s COM3 [baud]\n", argv[0]);
        return 1;
    }
    DWORD baud = (argc > 2) ? (DWORD)strtoul(argv[2], NULL, 10) : 115200;

    SetConsoleCtrlHandler(on_ctrl, TRUE);

    serial_port_t *sp = serial_open(argv[1], baud);
    if (!sp) return 1;

    printf("Reading from %s at %lu baud. Ctrl+C to quit.\n", argv[1], baud);

    unsigned char buf[512];
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int n = serial_read(sp, buf, sizeof(buf), 100);
        if (n < 0) break;
        if (n == 0){
            Sleep(1);
            continue;
        } 

        for (int i = 0; i < n; i++) printf("%02X ", buf[i]);
        printf("\n");
        fflush(stdout);
    }

    serial_close(sp);
    printf("\nClosed.\n");
    return 0;
}