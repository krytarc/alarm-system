#ifndef SERVER_NET_H
#define SERVER_NET_H

#include "devices.h"

// these functions are called when something happens
// GUI registers them so it can update itself
typedef void (*LogCallback)(const char *msg, void *data);
typedef void (*AlarmCallback)(const char *device, const char *text, void *data);
typedef void (*DeviceChangedCallback)(void *data);

typedef struct {
    int port;
    DeviceRegistry *registry;

    LogCallback on_log;
    AlarmCallback on_alarm;
    DeviceChangedCallback on_device_changed;
    void *cb_data;

    int server_fd;
    int running;
} ServerContext;

int  server_start(ServerContext *ctx);
void server_stop(ServerContext *ctx);

#endif
