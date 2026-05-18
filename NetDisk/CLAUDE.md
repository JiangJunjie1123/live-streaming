# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build / Run

```bash
# Option 1: qmake (Qt project file)
qmake NetDisk.pro && make

# Option 2: raw Makefile in src/
cd src && make          # builds the `server` binary
cd src && make clean    # remove objects and binary
cd src && make install  # copies server to /usr/bin/ (needs sudo)
```

The project links against `-lpthread -lmysqlclient`. MySQL client libraries must be installed.
Run the binary with an optional port: `./server 8000`

## Architecture

This is a **Linux C++ TCP server** for a video-sharing "NetDisk" platform. Qt is listed in the `.pro` file but explicitly disabled (`CONFIG -= qt`); the project is a pure console application using epoll, pthreads, and the MySQL C API.

### Layer stack

```
main.cpp
  └─ TcpKernel (singleton)          — orchestrator, owns the three subsystems below
       ├─ Block_Epoll_Net           — epoll event loop + accept/recv/send
       │    └─ thread_pool          — producer-consumer thread pool (auto-scaling)
       ├─ CMysql                    — thread-safe MySQL wrapper (mutex per query)
       └─ CLogic                    — business logic: register, login, upload
```

### Data flow

1. `main` creates `TcpKernel::GetInstance()`, calls `Open(port)` which inits MySQL + epoll, then `EventLoop()`.
2. `Block_Epoll_Net::EventLoop()` runs an infinite `epoll_wait` loop.
3. New connections get an `EPOLLIN|EPOLLONESHOT` registration (one-shot so each recv is serialized per socket).
4. On readable event → `recv_event` → thread pool worker calls `recv_task` which reads the packet and calls back to `TcpKernel::DealData`.
5. `TcpKernel::DealData` reads the 4-byte `PackType` header and dispatches via a function-pointer map (`m_NetPackMap`) to the corresponding `CLogic` handler.
6. `CLogic` handlers interact with `CMysql` and send responses via `m_tcp->SendData()`.

### Wire protocol (binary, packed structs)

```
| 4 bytes: payload size | 4 bytes: PackType | struct body (variable) |
```

- `SendData` prepends the 4-byte size before the struct.
- `recv_task` reads the 4-byte size first, then the struct.
- All packet structs are defined in `include/packdef.h` with `#pragma pack(push, 1)`.
- Packet types start at `_DEF_PACK_BASE` (10000): register (10000–10001), login (10002–10003), upload RQ/RS (10004–10005), file block (10006), recommend RQ/RS (10007–10008), download RQ/RS (10009–10010), download block RQ/RS (10011–10012).

### Key files

| File | Role |
|------|------|
| `include/packdef.h` | All protocol structs, packet type constants, config defines (port, buffer sizes, DB credentials) |
| `include/TCPKernel.h` / `src/TCPKernel.cpp` | Singleton kernel, packet dispatch via function pointer map |
| `include/block_epoll_net.h` / `src/block_epoll_net.cpp` | epoll networking, thread-safe `MyMap`, send/recv, one-shot event registration |
| `include/clogic.h` / `src/clogic.cpp` | Register, login, file upload (block-by-block with filesystem + MySQL insert) |
| `include/Thread_pool.h` / `src/Thread_pool.cpp` | Dynamic thread pool with manager thread (auto-scales 10–200 threads, 50000 queue max) |
| `include/Mysql.h` / `src/Mysql.cpp` | MySQL connect/select/update with per-call mutex locking |
| `src/main.cpp` | Entry point — port from argv, start kernel, enter event loop |

### Protocol dispatch pattern

New packet types are added by:
1. Define the struct in `packdef.h` with `_DEF_PACK_*_RQ` / `_DEF_PACK_*_RS` constants.
2. Add a handler method to `CLogic`.
3. Register the mapping in `CLogic::setNetPackMap()` via `NetPackMap(type) = &CLogic::Handler;`.

### Important details

- **EPOLLONESHOT**: Each client fd is re-registered for `EPOLLIN` after a recv completes (`recv_task` line 186). This prevents concurrent reads on the same socket from different threads.
- **MySQL credentials** are hardcoded in `packdef.h` (`_DEF_DB_*`). Database name: `VideoServer`.
- **File storage root**: `/home/colin/video/flv/<username>/` (hardcoded in `clogic.cpp`).
- **Thread safety**: `MyMap` wraps `std::map` with `pthread_mutex_t`; `CMysql` uses its own mutex; the thread pool uses cond vars + mutex.
- **Currently implemented handlers**: Register, Login, Upload (RQ + block transfer). Recommend and Download packet types are defined but not yet wired in `setNetPackMap()`.
- The Makefile in `src/` uses `g++` with `-std=gnu++11`. Object files build to `src/`, output binary is `src/server`.
