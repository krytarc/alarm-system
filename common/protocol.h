#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_MSG_LEN  512
#define MAX_NAME_LEN 64
#define MAX_TEXT_LEN 256
#define DEFAULT_PORT 8080

// types of messages
typedef enum {
    MSG_REGISTER,   // client sends its name to server
    MSG_OK,         // server says ok
    MSG_DENIED,     // server says no (reason in text field)
    MSG_ALARM,      // client detected something
    MSG_ACK,        // server got the alarm
    MSG_PING,       // check if still alive
    MSG_PONG,       // response to ping
    MSG_DISCONNECT, // client is leaving
    MSG_UNKNOWN
} MsgType;

// one message
typedef struct {
    MsgType type;
    char device[MAX_NAME_LEN];
    char text[MAX_TEXT_LEN];
} Message;

int proto_encode(const Message *msg, char *buf, int buf_size);
int proto_decode(const char *buf, Message *msg);
const char *proto_type_str(MsgType type);

#endif
