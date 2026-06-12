#ifndef CLIENT_NET_H
#define CLIENT_NET_H

#include "../common/protocol.h"

typedef void (*ClientLogCallback)(const char *msg, void *data);
typedef void (*ClientStatusCallback)(int connected, void *data);

typedef struct {
    char host[128];
    int port;
    char device_name[MAX_NAME_LEN];

    ClientLogCallback on_log;
    ClientStatusCallback on_status;
    void *cb_data;

    int sock_fd;
    int running;
    int ack_received; // set to 1 by recv_thread when ACK arrives
} ClientContext;

// returns 0 on success, -1 connection error, -2 registration denied
int  client_connect(ClientContext *ctx, char *err_out, int err_len);
void client_disconnect(ClientContext *ctx);
int  client_send_alarm(ClientContext *ctx, const char *text);
int  client_ping(ClientContext *ctx);

#endif
