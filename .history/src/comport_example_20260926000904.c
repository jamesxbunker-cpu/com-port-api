/* src/comport_example.c - direct COM port usage demo */
#include "comport.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG g_running = 1;

static BOOL WINAPI on_ctrl(DWORD type) {
    (void)type;
    InterlockedExchange(&g_running, 0);
    return TRUE;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s COM3 [baud]\n", argv[0]);
        return 1;
    }
    unsigned baud = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 10) : 115200;

    SetConsoleCtrlHandler(on_ctrl, TRUE);

    comport_t *c = comport_open(argv[1], baud);
    if (!c) return 1;

    printf("Opened %s at %u baud. Ctrl+C to quit.\n", comport_name(c), baud);

    unsigned char buf[256];
    unsigned long total = 0;

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int n = comport_read(c, buf, sizeof(buf), 1000);
        if (n < 0) break;
        if (n == 0) continue;
        total += (unsigned long)n;
        printf("read %d bytes (total %lu)\n", n, total);
    }

    comport_close(c);
    printf("Done. %lu bytes total.\n", total);
    return 0;
}