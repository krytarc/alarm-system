#ifndef DEVICES_H
#define DEVICES_H

#include <pthread.h>
#include "../common/protocol.h"

#define MAX_DEVICES 32

typedef enum {
    DISCONNECTED,
    CONNECTED
} DeviceStatus;

typedef struct {
    char name[MAX_NAME_LEN];
    DeviceStatus status;
    int sock; // -1 if not connected
} Device;

// list of all known devices + mutex to avoid race conditions
typedef struct {
    Device list[MAX_DEVICES];
    int count;
    pthread_mutex_t lock;
} DeviceRegistry;

void devices_init(DeviceRegistry *reg);
void devices_destroy(DeviceRegistry *reg);

int  devices_is_registered(DeviceRegistry *reg, const char *name);
int  devices_add(DeviceRegistry *reg, const char *name);
void devices_remove(DeviceRegistry *reg, const char *name);

int  devices_is_connected(DeviceRegistry *reg, const char *name);
int  devices_set_connected(DeviceRegistry *reg, const char *name, int sock);
int  devices_set_disconnected(DeviceRegistry *reg, const char *name);

// copy list without holding lock (so GUI can read it safely)
int  devices_snapshot(DeviceRegistry *reg, Device *out, int max);

#endif
