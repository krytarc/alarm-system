# Alarm System 🚨

A network-based home security alarm system with a client-server architecture built in C using GTK+ for the GUI.

## Overview

This project implements an anti-theft alarm system where multiple client devices (sensors) can connect to a central server to report security threats. The system communicates over TCP with a custom text-based protocol.

**Key Features:**
- Multi-client architecture with concurrent connection handling
- Custom TCP protocol for device communication
- Real-time alarm notifications on the server
- Device registration and authentication
- Heartbeat monitoring (PING/PONG)

## Architecture

### System Components

```
┌─────────────────────────────────────┐
│         Server (server_app)         │
│  - Manages connected devices        │
│  - Receives and displays alarms     │
│  - Plays notification sounds        │
└─────────────────────────────────────┘
         ▲                 ▲
         │ TCP             │ TCP
         │                 │
    ┌────┴─────┐      ┌────┴─────┐
    │ Client 1  │      │ Client 2  │
    │ (Sensor)  │      │ (Sensor)  │
    └───────────┘      └───────────┘
```

### Workflow

1. Server registers known device names in its configuration
2. Server starts and listens for incoming connections
3. Client connects and sends `REGISTER` message with its name
4. Server validates the name and responds with `OK` or `DENIED`
5. Authenticated clients can send alarm messages
6. Server displays alarms and plays notification sounds

## Communication Protocol

Custom text-based protocol using pipe-separated fields:

```
TYPE|device_name|content
```

### Message Types

| Message | Direction | Purpose |
|---------|-----------|---------|
| `REGISTER\|name\|` | Client → Server | Client registration |
| `OK\|name\|` | Server → Client | Registration accepted |
| `DENIED\|name\|reason` | Server → Client | Registration rejected |
| `ALARM\|name\|content` | Client → Server | Send security alarm |
| `ACK\|name\|` | Server → Client | Alarm acknowledged |
| `PING\|\|` | Client → Server | Heartbeat check |
| `PONG\|\|` | Server → Client | Heartbeat response |
| `DISCONNECT\|name\|` | Client → Server | Disconnect request |

## Project Structure

```
alarm-system/
├── common/
│   ├── protocol.c          # Message encoding/decoding
│   └── protocol.h
├── server/
│   ├── main.c              # Server GUI window
│   ├── server_net.c        # TCP socket and client threads
│   ├── server_net.h
│   ├── devices.c           # Device registry with mutex protection
│   └── devices.h
├── client/
│   ├── main.c              # Client GUI window
│   ├── client_net.c        # Server connection and alarm sending
│   └── client_net.h
├── Makefile
├── README.md               # This file
└── docs/
    └── docs.md             # Detailed project documentation (Polish)
```

## Building

### Requirements

- GCC compiler
- GTK+ 3.0 development libraries
- POSIX threads (pthreads)
- Make

### Compilation

```bash
make           # Build both server and client
make clean     # Remove compiled binaries
```

This generates:
- `server_app` - Server executable
- `client_app` - Client executable

## Running

### Start Server

```bash
./server_app
```

The server will display a list of connected devices and received alarms.

### Connect Clients

In separate terminals:

```bash
./client_app
```

Each client will prompt you to enter:
- Device name
- Server address (IP/hostname)
- Server port

## Technical Highlights

### Threading
- **Server**: Creates a detached thread for each client connection to handle concurrent communications
- **Client**: Background thread monitors server responses while the main thread handles UI interactions

### Thread Safety
- Device registry protected by `pthread_mutex_t` to prevent data races
- Mutex acquired before modifying shared device list
- Snapshot function copies device state without blocking GUI

### Signal Handling
- Uses `MSG_NOSIGNAL` flag with `send()` to prevent SIGPIPE crashes when server disconnects unexpectedly
- Graceful degradation when connection is lost

### Socket Configuration
- `SO_REUSEADDR` and `SO_REUSEPORT` enabled for immediate server restart capability
- TCP listen backlog set to 8 concurrent connections

## Usage Examples

### Server Setup

1. Launch server application
2. Add known device names to the configuration
3. Monitor incoming alarms in real-time

### Client Setup

1. Launch client application
2. Enter a registered device name
3. Enter server address and port
4. Once connected, trigger alarms through the GUI

## Implementation Details

For detailed technical documentation including protocol implementation, threading architecture, and mutex synchronization strategies, see [docs/docs.md](docs/docs.md) (Polish language).


