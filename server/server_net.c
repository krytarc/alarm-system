#include "server_net.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// data passed to each client thread
typedef struct {
    int client_fd;
    char ip[INET_ADDRSTRLEN];
    ServerContext *ctx;
} ClientArgs;

// helper to send a message
static void send_msg(int fd, MsgType type, const char *device, const char *text) {
    Message m;
    char buf[MAX_MSG_LEN];
    m.type = type;
    strncpy(m.device, device ? device : "", MAX_NAME_LEN - 1);
    strncpy(m.text, text ? text : "", MAX_TEXT_LEN - 1);
    proto_encode(&m, buf, sizeof(buf));
    send(fd, buf, strlen(buf), 0);
}

// one thread per connected client
static void *client_thread(void *arg) {
    ClientArgs *ca = (ClientArgs *)arg;
    ServerContext *ctx = ca->ctx;
    int fd = ca->client_fd;
    char ip[INET_ADDRSTRLEN];
    strncpy(ip, ca->ip, INET_ADDRSTRLEN);
    free(ca);

    char buf[MAX_MSG_LEN];
    char dev_name[MAX_NAME_LEN] = "";
    char log_buf[512];

    snprintf(log_buf, sizeof(log_buf), "new connection from %s", ip);
    if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);

    while (ctx->running) {
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        Message msg;
        if (proto_decode(buf, &msg) < 0) continue;

        if (msg.type == MSG_REGISTER) {
            if (!devices_is_registered(ctx->registry, msg.device)) {
                send_msg(fd, MSG_DENIED, msg.device, "not_registered");
                snprintf(log_buf, sizeof(log_buf), "denied: %s not on the list", msg.device);
                if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);
            } else if (devices_is_connected(ctx->registry, msg.device)) {
                send_msg(fd, MSG_DENIED, msg.device, "already_connected");
                snprintf(log_buf, sizeof(log_buf), "denied: %s already connected", msg.device);
                if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);
            } else {
                devices_set_connected(ctx->registry, msg.device, fd);
                strncpy(dev_name, msg.device, MAX_NAME_LEN - 1);
                send_msg(fd, MSG_OK, msg.device, "");
                snprintf(log_buf, sizeof(log_buf), "device connected: %s", msg.device);
                if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);
                if (ctx->on_device_changed) ctx->on_device_changed(ctx->cb_data);
            }
        } else if (msg.type == MSG_ALARM) {
            // check if device is still on the list (could have been removed)
            if (!devices_is_registered(ctx->registry, dev_name)) {
                send_msg(fd, MSG_DENIED, dev_name, "device_removed");
                break;
            }
            snprintf(log_buf, sizeof(log_buf), "ALARM from %s: %s", msg.device, msg.text);
            if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);
            if (ctx->on_alarm) ctx->on_alarm(msg.device, msg.text, ctx->cb_data);
            send_msg(fd, MSG_ACK, msg.device, "");
        } else if (msg.type == MSG_PING) {
            send_msg(fd, MSG_PONG, "", "");
        } else if (msg.type == MSG_DISCONNECT) {
            break;
        }
    }

    // cleanup when client disconnects
    if (dev_name[0] != '\0') {
        devices_set_disconnected(ctx->registry, dev_name);
        snprintf(log_buf, sizeof(log_buf), "device disconnected: %s", dev_name);
        if (ctx->on_log) ctx->on_log(log_buf, ctx->cb_data);
        if (ctx->on_device_changed) ctx->on_device_changed(ctx->cb_data);
    }
    close(fd);
    return NULL;
}

// main server thread - waits for new connections
static void *accept_thread(void *arg) {
    ServerContext *ctx = (ServerContext *)arg;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    while (ctx->running) {
        int client_fd = accept(ctx->server_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) break; // server was stopped

        ClientArgs *ca = malloc(sizeof(ClientArgs));
        ca->client_fd = client_fd;
        ca->ctx = ctx;
        inet_ntop(AF_INET, &addr.sin_addr, ca->ip, INET_ADDRSTRLEN);

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, ca);
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

int server_start(ServerContext *ctx) {
    ctx->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->server_fd < 0) return -1;

    // SO_REUSEADDR + SO_REUSEPORT so we can restart quickly without "address already in use"
    int opt = 1;
    setsockopt(ctx->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(ctx->server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ctx->port);

    if (bind(ctx->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ctx->server_fd);
        ctx->server_fd = -1;
        return -1;
    }
    if (listen(ctx->server_fd, 8) < 0) {
        close(ctx->server_fd);
        ctx->server_fd = -1;
        return -1;
    }

    ctx->running = 1;
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, accept_thread, ctx);
    pthread_attr_destroy(&attr);
    return 0;
}

void server_stop(ServerContext *ctx) {
    ctx->running = 0;
    if (ctx->server_fd >= 0) {
        close(ctx->server_fd); // this makes accept() return -1 and the thread exits
        ctx->server_fd = -1;
    }
}
