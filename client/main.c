#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#include "client_net.h"
#include "../common/protocol.h"

// global state for this process
static ClientContext g_client;

static GtkWidget *g_window;
static GtkWidget *g_entry_name;
static GtkWidget *g_entry_host;
static GtkWidget *g_entry_port;
static GtkWidget *g_entry_msg;
static GtkWidget *g_btn_connect;
static GtkWidget *g_btn_alarm;
static GtkWidget *g_btn_ping;
static GtkWidget *g_label_status;
static GtkTextBuffer *g_log_buf;
static GtkWidget *g_log_view;

static void get_time(char *buf, int len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%H:%M:%S", tm);
}

static void append_log(const char *msg) {
    char ts[16];
    get_time(ts, sizeof(ts));

    char line[512];
    snprintf(line, sizeof(line), "[%s] %s\n", ts, msg);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    gtk_text_buffer_insert(g_log_buf, &end, line, -1);

    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(g_log_buf, "end");
    if (!mark)
        mark = gtk_text_buffer_create_mark(g_log_buf, "end", &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g_log_view), mark, 0.0, FALSE, 0, 0);
}

static void set_ui_connected(int connected) {
    if (connected) {
        gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='green'>🟢 connected</span>");
        gtk_button_set_label(GTK_BUTTON(g_btn_connect), "Disconnect");
        gtk_widget_set_sensitive(g_btn_alarm, TRUE);
        gtk_widget_set_sensitive(g_btn_ping, TRUE);
        gtk_widget_set_sensitive(g_entry_name, FALSE);
        gtk_widget_set_sensitive(g_entry_host, FALSE);
        gtk_widget_set_sensitive(g_entry_port, FALSE);
    } else {
        gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='red'>🔴 disconnected</span>");
        gtk_button_set_label(GTK_BUTTON(g_btn_connect), "Connect");
        gtk_widget_set_sensitive(g_btn_alarm, FALSE);
        gtk_widget_set_sensitive(g_btn_ping, FALSE);
        gtk_widget_set_sensitive(g_entry_name, TRUE);
        gtk_widget_set_sensitive(g_entry_host, TRUE);
        gtk_widget_set_sensitive(g_entry_port, TRUE);
    }
}

typedef struct { char msg[512]; } IdleLog;
typedef struct { int connected; } IdleStatus;

static gboolean do_log(gpointer data) {
    append_log(((IdleLog *)data)->msg);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gboolean do_status(gpointer data) {
    set_ui_connected(((IdleStatus *)data)->connected);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void cb_log(const char *msg, void *data) {
    (void)data;
    IdleLog *il = g_new(IdleLog, 1);
    strncpy(il->msg, msg, sizeof(il->msg) - 1);
    g_idle_add(do_log, il);
}

static void cb_status(int connected, void *data) {
    (void)data;
    IdleStatus *is = g_new(IdleStatus, 1);
    is->connected = connected;
    g_idle_add(do_status, is);
}

static void on_connect_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;

    if (g_client.running) {
        client_disconnect(&g_client);
        append_log("disconnected");
        set_ui_connected(0);
        return;
    }

    const char *name = gtk_entry_get_text(GTK_ENTRY(g_entry_name));
    const char *host = gtk_entry_get_text(GTK_ENTRY(g_entry_host));
    int port = atoi(gtk_entry_get_text(GTK_ENTRY(g_entry_port)));

    if (strlen(name) == 0) {
        append_log("enter device name first");
        return;
    }

    strncpy(g_client.host, host, sizeof(g_client.host) - 1);
    strncpy(g_client.device_name, name, MAX_NAME_LEN - 1);
    g_client.port = port;
    g_client.on_log = cb_log;
    g_client.on_status = cb_status;
    g_client.cb_data = NULL;

    char err[256] = "";
    if (client_connect(&g_client, err, sizeof(err)) < 0) {
        char msg[300];
        snprintf(msg, sizeof(msg), "error: %s", err);
        append_log(msg);
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "connected to %s:%d as %s", host, port, name);
    append_log(msg);
    set_ui_connected(1);
}

static void on_alarm_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;

    const char *text = gtk_entry_get_text(GTK_ENTRY(g_entry_msg));
    if (strlen(text) == 0) text = "test alarm";

    if (client_send_alarm(&g_client, text) < 0) {
        append_log("failed to send alarm - server disconnected?");
        set_ui_connected(0); // update buttons to reflect lost connection
        return;
    }
    char msg[512];
    snprintf(msg, sizeof(msg), "alarm sent: %s", text);
    append_log(msg);
}

static void on_ping_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    client_ping(&g_client);
    append_log("ping sent");
}

static gboolean on_close(GtkWidget *w, GdkEvent *e, gpointer ud) {
    (void)w; (void)e; (void)ud;
    if (g_client.running) client_disconnect(&g_client);
    gtk_main_quit();
    return FALSE;
}

static void build_ui(void) {
    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "Alarm System - Client");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 400, 480);
    gtk_container_set_border_width(GTK_CONTAINER(g_window), 10);
    g_signal_connect(g_window, "delete-event", G_CALLBACK(on_close), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    // connection settings
    GtkWidget *frame1 = gtk_frame_new("Connect to server");
    gtk_box_pack_start(GTK_BOX(vbox), frame1, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(frame1), grid);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("device name:"), 0, 0, 1, 1);
    g_entry_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_entry_name), "e.g. sensor-hallway");
    gtk_widget_set_hexpand(g_entry_name, TRUE);
    gtk_grid_attach(GTK_GRID(grid), g_entry_name, 1, 0, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("server ip:"), 0, 1, 1, 1);
    g_entry_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(g_entry_host), "127.0.0.1");
    gtk_widget_set_hexpand(g_entry_host, TRUE);
    gtk_grid_attach(GTK_GRID(grid), g_entry_host, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("port:"), 0, 2, 1, 1);
    g_entry_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(g_entry_port), "8080");
    gtk_entry_set_width_chars(GTK_ENTRY(g_entry_port), 6);
    gtk_grid_attach(GTK_GRID(grid), g_entry_port, 1, 2, 1, 1);

    g_label_status = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='red'>🔴 disconnected</span>");
    gtk_grid_attach(GTK_GRID(grid), g_label_status, 0, 3, 2, 1);

    g_btn_connect = gtk_button_new_with_label("Connect");
    g_signal_connect(g_btn_connect, "clicked", G_CALLBACK(on_connect_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), g_btn_connect, 2, 3, 1, 1);

    // alarm section
    GtkWidget *frame2 = gtk_frame_new("Send alarm");
    gtk_box_pack_start(GTK_BOX(vbox), frame2, FALSE, FALSE, 0);

    GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox2), 10);
    gtk_container_add(GTK_CONTAINER(frame2), vbox2);

    g_entry_msg = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_entry_msg), "alarm message...");
    gtk_box_pack_start(GTK_BOX(vbox2), g_entry_msg, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

    g_btn_alarm = gtk_button_new_with_label("🚨 Send alarm");
    gtk_widget_set_sensitive(g_btn_alarm, FALSE);
    g_signal_connect(g_btn_alarm, "clicked", G_CALLBACK(on_alarm_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), g_btn_alarm, TRUE, TRUE, 0);

    g_btn_ping = gtk_button_new_with_label("Ping");
    gtk_widget_set_sensitive(g_btn_ping, FALSE);
    g_signal_connect(g_btn_ping, "clicked", G_CALLBACK(on_ping_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), g_btn_ping, FALSE, FALSE, 0);

    // log
    GtkWidget *frame3 = gtk_frame_new("Log");
    gtk_box_pack_start(GTK_BOX(vbox), frame3, TRUE, TRUE, 0);

    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(sw), 4);
    gtk_container_add(GTK_CONTAINER(frame3), sw);

    g_log_buf = gtk_text_buffer_new(NULL);
    g_log_view = gtk_text_view_new_with_buffer(g_log_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_log_view), TRUE);
    gtk_container_add(GTK_CONTAINER(sw), g_log_view);

    gtk_widget_show_all(g_window);
    append_log("ready. enter device name and connect");
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    memset(&g_client, 0, sizeof(g_client));
    g_client.sock_fd = -1;

    build_ui();
    gtk_main();
    return 0;
}
