#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

static void on_webview_load_changed(WebKitWebView *webview, WebKitLoadEvent event, gpointer user_data) {
    // Handle load events here, for example to show loading spinner
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *view;

    window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_set_title(GTK_WINDOW(window), "Spectral Browser");

    g_signal_connect(app, "destroy", G_CALLBACK(gtk_widget_destroy), window);

    view = webkit_web_view_new();

    g_signal_connect(view, "load-changed", G_CALLBACK(on_webview_load_changed), NULL);
    gtk_container_add(GTK_CONTAINER(window), view);

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.example.spectralbrowser", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
