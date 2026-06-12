#include "protocol.h"
#include <stdio.h>
#include <string.h>

// same order as enum
static const char *names[] = {
    "REGISTER", "OK", "DENIED", "ALARM",
    "ACK", "PING", "PONG", "DISCONNECT", "UNKNOWN"
};

const char *proto_type_str(MsgType type) {
    if (type < 0 || type > MSG_UNKNOWN) return "UNKNOWN";
    return names[type];
}

// format: TYPE|device|text\n
int proto_encode(const Message *msg, char *buf, int buf_size) {
    return snprintf(buf, buf_size, "%s|%s|%s\n",
        proto_type_str(msg->type), msg->device, msg->text);
}

int proto_decode(const char *buf, Message *msg) {
    char tmp[MAX_MSG_LEN];
    strncpy(tmp, buf, MAX_MSG_LEN - 1);

    // remove newline at end
    int len = strlen(tmp);
    if (tmp[len-1] == '\n') tmp[len-1] = '\0';

    char type_str[32];

    char *tok = strtok(tmp, "|");
    if (!tok) return -1;
    strncpy(type_str, tok, 31);

    tok = strtok(NULL, "|");
    if (!tok) return -1;
    strncpy(msg->device, tok, MAX_NAME_LEN - 1);

    tok = strtok(NULL, "|");
    if (tok)
        strncpy(msg->text, tok, MAX_TEXT_LEN - 1);
    else
        msg->text[0] = '\0';

    // find matching type
    msg->type = MSG_UNKNOWN;
    int n = sizeof(names) / sizeof(names[0]);
    for (int i = 0; i < n; i++) {
        if (strcmp(type_str, names[i]) == 0) {
            msg->type = (MsgType)i;
            break;
        }
    }

    return 0;
}
