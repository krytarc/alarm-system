#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "server_net.h"
#include "devices.h"
#include "../common/protocol.h"

static void append_log(const char *msg);
static void refresh_device_list(void);
static void on_remove_clicked(GtkButton *btn, gpointer ud);

static DeviceRegistry g_registry;
static ServerContext g_server;

static GtkWidget *g_window;
static GtkWidget *g_btn_start;
static GtkWidget *g_entry_port;
static GtkWidget *g_entry_devname;
static GtkWidget *g_label_status;
static GtkWidget *g_device_list;
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

    // scroll down
    gtk_text_buffer_get_end_iter(g_log_buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(g_log_buf, "end");
    if (!mark)
        mark = gtk_text_buffer_create_mark(g_log_buf, "end", &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(g_log_view), mark, 0.0, FALSE, 0, 0);
}

static void refresh_device_list(void) {
    // clear old list
    GList *ch = gtk_container_get_children(GTK_CONTAINER(g_device_list));
    for (GList *l = ch; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);

    Device snap[MAX_DEVICES];
    int n = devices_snapshot(&g_registry, snap, MAX_DEVICES);

    for (int i = 0; i < n; i++) {
        GtkWidget *row  = gtk_list_box_row_new();
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);

        char label[MAX_NAME_LEN + 8];
        if (snap[i].status == CONNECTED)
            snprintf(label, sizeof(label), "🟢 %s", snap[i].name);
        else
            snprintf(label, sizeof(label), "🔴 %s", snap[i].name);

        GtkWidget *lbl = gtk_label_new(label);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);

        GtkWidget *btn = gtk_button_new_with_label("x");
        g_object_set_data(G_OBJECT(btn), "name", g_strdup(snap[i].name));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_remove_clicked), NULL);
        gtk_box_pack_end(GTK_BOX(hbox), btn, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), hbox);
        gtk_container_add(GTK_CONTAINER(g_device_list), row);
    }
    gtk_widget_show_all(g_device_list);
}

// structs for passing data to GTK main thread via g_idle_add
// (you cant call GTK functions from other threads directly)
typedef struct { char msg[512]; } IdleLog;
typedef struct { char dev[MAX_NAME_LEN]; char text[MAX_TEXT_LEN]; } IdleAlarm;

static gboolean do_log(gpointer data) {
    append_log(((IdleLog *)data)->msg);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gboolean do_refresh(gpointer data) {
    (void)data;
    refresh_device_list();
    return G_SOURCE_REMOVE;
}

static gboolean do_alarm(gpointer data) {
    IdleAlarm *ia = (IdleAlarm *)data;

    system("aplay /usr/share/sounds/alsa/Front_Center.wav 2>/dev/null &");

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "!! ALARM !!", GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
        "OK", GTK_RESPONSE_OK, NULL);
    gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);

    char ts[16];
    get_time(ts, sizeof(ts));

    char txt[512];
    snprintf(txt, sizeof(txt),
        "<big><b>🚨 ALARM 🚨</b></big>\n\n"
        "Device: %s\nTime: %s\nMessage: %s",
        ia->dev, ts, ia->text);

    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), txt);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    gtk_container_add(GTK_CONTAINER(content), lbl);
    gtk_widget_show_all(dlg);

    g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    g_free(ia);
    return G_SOURCE_REMOVE;
}

// called from network thread - schedule GTK update
static void cb_log(const char *msg, void *data) {
    (void)data;
    IdleLog *il = g_new(IdleLog, 1);
    strncpy(il->msg, msg, sizeof(il->msg) - 1);
    g_idle_add(do_log, il);
}

static void cb_alarm(const char *dev, const char *text, void *data) {
    (void)data;
    IdleAlarm *ia = g_new(IdleAlarm, 1);
    strncpy(ia->dev, dev, MAX_NAME_LEN - 1);
    strncpy(ia->text, text, MAX_TEXT_LEN - 1);
    g_idle_add(do_alarm, ia);
}

static void cb_device_changed(void *data) {
    (void)data;
    g_idle_add(do_refresh, NULL);
}

static void on_start_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;

    if (g_server.running) {
        server_stop(&g_server);
        gtk_button_set_label(GTK_BUTTON(g_btn_start), "Start server");
        gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='red'>● stopped</span>");
        append_log("server stopped");
        return;
    }

    int port = atoi(gtk_entry_get_text(GTK_ENTRY(g_entry_port)));
    if (port <= 0) port = DEFAULT_PORT;

    g_server.port = port;
    g_server.registry = &g_registry;
    g_server.on_log = cb_log;
    g_server.on_alarm = cb_alarm;
    g_server.on_device_changed = cb_device_changed;
    g_server.cb_data = NULL;

    if (server_start(&g_server) < 0) {
        append_log("ERROR: could not start server");
        return;
    }

    gtk_button_set_label(GTK_BUTTON(g_btn_start), "Stop server");
    gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='green'>● running</span>");

    char msg[128];
    snprintf(msg, sizeof(msg), "server started on port %d", port);
    append_log(msg);
}

static void on_add_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;

    const char *name = gtk_entry_get_text(GTK_ENTRY(g_entry_devname));
    if (strlen(name) == 0) return;

    if (devices_is_registered(&g_registry, name)) {
        append_log("device already exists");
        return;
    }
    if (devices_add(&g_registry, name) < 0) {
        append_log("too many devices");
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "added device: %s", name);
    append_log(msg);
    gtk_entry_set_text(GTK_ENTRY(g_entry_devname), "");
    refresh_device_list();
}

static void on_remove_clicked(GtkButton *btn, gpointer ud) {
    (void)ud;
    const char *name = g_object_get_data(G_OBJECT(btn), "name");
    devices_remove(&g_registry, name);
    char msg[128];
    snprintf(msg, sizeof(msg), "removed device: %s", name);
    append_log(msg);
    refresh_device_list();
}

static void build_ui(GtkApplication *app) {
    g_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(g_window), "Alarm System - Server");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 500, 620);
    gtk_container_set_border_width(GTK_CONTAINER(g_window), 10);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    // top: status + port + start button
    GtkWidget *hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_top, FALSE, FALSE, 0);

    g_label_status = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(g_label_status), "<span color='red'>● stopped</span>");
    gtk_box_pack_start(GTK_BOX(hbox_top), g_label_status, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox_top), gtk_label_new("port:"), FALSE, FALSE, 0);
    g_entry_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(g_entry_port), "8080");
    gtk_entry_set_width_chars(GTK_ENTRY(g_entry_port), 6);
    gtk_box_pack_start(GTK_BOX(hbox_top), g_entry_port, FALSE, FALSE, 0);

    g_btn_start = gtk_button_new_with_label("Start server");
    g_signal_connect(g_btn_start, "clicked", G_CALLBACK(on_start_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(hbox_top), g_btn_start, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // devices frame
    GtkWidget *frame = gtk_frame_new("Registered devices");
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox2), 8);
    gtk_container_add(GTK_CONTAINER(frame), vbox2);

    GtkWidget *hbox_add = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox_add, FALSE, FALSE, 0);

    g_entry_devname = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_entry_devname), "device name...");
    gtk_box_pack_start(GTK_BOX(hbox_add), g_entry_devname, TRUE, TRUE, 0);

    GtkWidget *btn_add = gtk_button_new_with_label("Add");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_add), btn_add, FALSE, FALSE, 0);

    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 130);
    gtk_box_pack_start(GTK_BOX(vbox2), sw, TRUE, TRUE, 0);

    g_device_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_device_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sw), g_device_list);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // log frame
    GtkWidget *frame_log = gtk_frame_new("Log");
    gtk_box_pack_start(GTK_BOX(vbox), frame_log, TRUE, TRUE, 0);

    GtkWidget *sw_log = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(sw_log), 4);
    gtk_container_add(GTK_CONTAINER(frame_log), sw_log);

    g_log_buf = gtk_text_buffer_new(NULL);
    g_log_view = gtk_text_view_new_with_buffer(g_log_buf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_log_view), TRUE);
    gtk_container_add(GTK_CONTAINER(sw_log), g_log_view);

    gtk_widget_show_all(g_window);
    append_log("ready. add devices and start the server");
}

static void on_activate(GtkApplication *app, gpointer ud) {
    (void)ud;
    devices_init(&g_registry);
    memset(&g_server, 0, sizeof(g_server));
    g_server.server_fd = -1;
    build_ui(app);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("pl.agh.alarm.server",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int ret = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    server_stop(&g_server);
    devices_destroy(&g_registry);
    return ret;
}
