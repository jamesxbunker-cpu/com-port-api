# com-port-api

A C library/utility for reading from Windows COM ports using overlapped I/O and event-driven waiting.

> ⚠️ **Work in Progress** — Actively being developed. Expect incomplete features and breaking changes.

## About

`com-port-api` wraps the Windows serial port API to make reading from COM ports simpler and more reliable. It uses overlapped (asynchronous) I/O with `WaitCommEvent` and `EV_RXCHAR` to avoid busy-waiting — the read only fires when data actually arrives.

## Status

- [x] Split into `main` and `serial` for organization
- [x] Switched to blocking reads driven by `EV_RXCHAR`
- [x] Fixed sleep function so read doesn't fire prematurely
- [ ] *(more to come)*

## Project Structure
```
com-port-api
├── inc/ # Header files
├── src/ # Source files (main, serial)
├── .history/ # Local editor history (not part of the build)
├── Makefile
├── readcom.exe # Compiled binary
└── README.md
```


## Getting Started

### Requirements

- A C compiler (GCC/MinGW recommended on Windows)
- `make`
- Windows (uses `windows.h`, `WaitCommEvent`, `EV_RXCHAR`)

### Build

```bash
make
./readcom.exe <COM_PORT> <BAUD>
```
