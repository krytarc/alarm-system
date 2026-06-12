#include "devices.h"
#include <string.h>
#include <unistd.h>

void devices_init(DeviceRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    pthread_mutex_init(&reg->lock, NULL);
    for (int i = 0; i < MAX_DEVICES; i++)
        reg->list[i].sock = -1;
}

void devices_destroy(DeviceRegistry *reg) {
    pthread_mutex_destroy(&reg->lock);
}

int devices_is_registered(DeviceRegistry *reg, const char *name) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->list[i].name, name) == 0) {
            pthread_mutex_unlock(&reg->lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&reg->lock);
    return 0;
}

int devices_add(DeviceRegistry *reg, const char *name) {
    pthread_mutex_lock(&reg->lock);
    if (reg->count >= MAX_DEVICES) {
        pthread_mutex_unlock(&reg->lock);
        return -1;
    }
    strncpy(reg->list[reg->count].name, name, MAX_NAME_LEN - 1);
    reg->list[reg->count].status = DISCONNECTED;
    reg->list[reg->count].sock = -1;
    reg->count++;
    pthread_mutex_unlock(&reg->lock);
    return 0;
}

void devices_remove(DeviceRegistry *reg, const char *name) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->list[i].name, name) == 0) {
            // if device is connected, close the socket so client gets disconnected
            if (reg->list[i].status == CONNECTED && reg->list[i].sock >= 0)
                close(reg->list[i].sock);
            // move everything one position back
            for (int j = i; j < reg->count - 1; j++)
                reg->list[j] = reg->list[j+1];
            reg->count--;
            break;
        }
    }
    pthread_mutex_unlock(&reg->lock);
}

int devices_is_connected(DeviceRegistry *reg, const char *name) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->list[i].name, name) == 0) {
            int result = (reg->list[i].status == CONNECTED);
            pthread_mutex_unlock(&reg->lock);
            return result;
        }
    }
    pthread_mutex_unlock(&reg->lock);
    return 0;
}

int devices_set_connected(DeviceRegistry *reg, const char *name, int sock) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->list[i].name, name) == 0) {
            reg->list[i].status = CONNECTED;
            reg->list[i].sock = sock;
            pthread_mutex_unlock(&reg->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&reg->lock);
    return -1;
}

int devices_set_disconnected(DeviceRegistry *reg, const char *name) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->list[i].name, name) == 0) {
            reg->list[i].status = DISCONNECTED;
            reg->list[i].sock = -1;
            pthread_mutex_unlock(&reg->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&reg->lock);
    return -1;
}

int devices_snapshot(DeviceRegistry *reg, Device *out, int max) {
    pthread_mutex_lock(&reg->lock);
    int n = reg->count;
    if (n > max) n = max;
    for (int i = 0; i < n; i++)
        out[i] = reg->list[i];
    pthread_mutex_unlock(&reg->lock);
    return n;
}
