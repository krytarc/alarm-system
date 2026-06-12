#include "client_net.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

static void send_msg(int fd, MsgType type, const char *device, const char *text) {
    Message m;
    char buf[MAX_MSG_LEN];
    m.type = type;
    strncpy(m.device, device ? device : "", MAX_NAME_LEN - 1);
    strncpy(m.text, text ? text : "", MAX_TEXT_LEN - 1);
    proto_encode(&m, buf, sizeof(buf));
    send(fd, buf, strlen(buf), 0);
}

// background thread that listens for messages from server
static void *recv_thread(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    char buf[MAX_MSG_LEN];

    while (ctx->running) {
        int n = recv(ctx->sock_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            ctx->running = 0;
            close(ctx->sock_fd); // close immediately so any pending send() fails
            ctx->sock_fd = -1;
            if (ctx->on_log) ctx->on_log("lost connection to server", ctx->cb_data);
            if (ctx->on_status) ctx->on_status(0, ctx->cb_data);
            break;
        }
        buf[n] = '\0';

        Message msg;
        if (proto_decode(buf, &msg) < 0) continue;

        if (msg.type == MSG_ACK) {
            ctx->ack_received = 1;
            if (ctx->on_log) ctx->on_log("server got the alarm (ACK)", ctx->cb_data);
        } else if (msg.type == MSG_DENIED) {
            // server rejected us, probably device was removed
            ctx->running = 0;
            close(ctx->sock_fd);
            ctx->sock_fd = -1;
            if (ctx->on_log) ctx->on_log("disconnected: device was removed from server", ctx->cb_data);
            if (ctx->on_status) ctx->on_status(0, ctx->cb_data);
            break;
        } else if (msg.type == MSG_PONG) {
            if (ctx->on_log) ctx->on_log("PONG from server", ctx->cb_data);
        }
    }
    return NULL;
}

int client_connect(ClientContext *ctx, char *err_out, int err_len) {
    struct hostent *he = gethostbyname(ctx->host);
    if (!he) {
        snprintf(err_out, err_len, "cannot resolve host: %s", ctx->host);
        return -1;
    }

    ctx->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->sock_fd < 0) {
        snprintf(err_out, err_len, "socket() failed");
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(ctx->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        snprintf(err_out, err_len, "cannot connect to %s:%d", ctx->host, ctx->port);
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
        return -1;
    }

    // send our name and wait for response
    send_msg(ctx->sock_fd, MSG_REGISTER, ctx->device_name, "");

    char buf[MAX_MSG_LEN];
    int n = recv(ctx->sock_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        snprintf(err_out, err_len, "no response from server");
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
        return -1;
    }
    buf[n] = '\0';

    Message resp;
    proto_decode(buf, &resp);

    if (resp.type != MSG_OK) {
        snprintf(err_out, err_len, "registration denied: %s", resp.text);
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
        return -2;
    }

    // start listening for messages in background
    ctx->running = 1;
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, recv_thread, ctx);
    pthread_attr_destroy(&attr);

    if (ctx->on_status) ctx->on_status(1, ctx->cb_data);
    return 0;
}

void client_disconnect(ClientContext *ctx) {
    if (!ctx->running) return;
    ctx->running = 0;
    send_msg(ctx->sock_fd, MSG_DISCONNECT, ctx->device_name, "");
    close(ctx->sock_fd);
    ctx->sock_fd = -1;
    if (ctx->on_status) ctx->on_status(0, ctx->cb_data);
}

int client_send_alarm(ClientContext *ctx, const char *text) {
    if (!ctx->running || ctx->sock_fd < 0) return -1;

    Message m;
    char buf[MAX_MSG_LEN];
    m.type = MSG_ALARM;
    strncpy(m.device, ctx->device_name, MAX_NAME_LEN - 1);
    strncpy(m.text, text, MAX_TEXT_LEN - 1);
    proto_encode(&m, buf, sizeof(buf));

    int n = send(ctx->sock_fd, buf, strlen(buf), MSG_NOSIGNAL);
    if (n <= 0) {
        ctx->running = 0;
        return -1;
    }

    // wait up to 2 seconds for ACK from server
    // if no ACK, server is probably dead
    ctx->ack_received = 0;
    int waited = 0;
    while (waited < 2000) {
        if (!ctx->running) return -1; // recv_thread detected disconnect
        if (ctx->ack_received) return 0; // got ACK, all good
        usleep(50000); // wait 50ms
        waited += 50;
    }

    // timeout - no ACK means server is gone
    ctx->running = 0;
    return -1;
}

int client_ping(ClientContext *ctx) {
    if (!ctx->running || ctx->sock_fd < 0) return -1;
    send_msg(ctx->sock_fd, MSG_PING, ctx->device_name, "");
    return 0;
}
