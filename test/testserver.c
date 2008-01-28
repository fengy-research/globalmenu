#include <gtk/gtk.h>
#include <libgnomenu/gdksocket.h>
#include <libgnomenu/gnomenuserver.h>


static void create_clicked_event_cb(GtkWidget * button, GdkSocket * socket){
	GnomenuMessage msg;
	msg.any.type = GNOMENU_MSG_CLIENT_NEW;
	msg.client_new.socket_id = gdk_socket_get_native(socket);
	msg.client_new.ui_window =0xdeadbeaf;
	msg.client_new.parent_window =0xbeafdead;
	gdk_socket_send_by_name(socket, GNOMENU_SERVER_NAME, &msg, sizeof(msg));
}
static void destroy_clicked_event_cb(GtkWidget * button, GdkSocket * socket){
	GnomenuMessage msg;
	msg.any.type = GNOMENU_MSG_CLIENT_DESTROY;
	msg.client_new.socket_id = gdk_socket_get_native(socket);
	msg.client_new.ui_window =0xdeadbeaf;
	msg.client_new.parent_window =0xbeafdead;
	gdk_socket_send_by_name(socket, GNOMENU_SERVER_NAME, &msg, sizeof(msg));

}
static void window_destroy_event_cb(GtkWidget * window, GdkEvent * ev, gpointer user_data){
	gtk_main_quit();
}

int main(int argc, char* argv[]){
	GtkWindow * window;
	GnomenuServer * server;
	GtkButton * create, * destroy;
	GdkSocket * socket;
	GtkBox * box;

	gtk_init(&argc, &argv);

	socket = gdk_socket_new("test socket");

	window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	server = gnomenu_server_new();
	create = GTK_BUTTON(gtk_button_new_with_label("create client"));
	destroy = GTK_BUTTON(gtk_button_new_with_label("destroy client"));

	box = GTK_BOX(gtk_vbox_new(FALSE, 0));
	
	g_signal_connect(G_OBJECT(window), "destroy",
			window_destroy_event_cb, NULL);

	g_signal_connect(G_OBJECT(create), "clicked",
			create_clicked_event_cb, socket);
	g_signal_connect(G_OBJECT(destroy), "clicked",
			destroy_clicked_event_cb, socket);
	
	gtk_container_add(window, box);

	gtk_box_pack_start_defaults(box, create);
	gtk_box_pack_start_defaults(box, destroy);
	gtk_widget_show_all(GTK_WIDGET(window));
	gtk_main();
//	g_object_unref(server);
//	g_object_unref(socket);

	return 0;
}
