#define WNCK_I_KNOW_THIS_IS_UNSTABLE

#include <X11/Xatom.h>
#include <errno.h>
#include <gdk/gdkx.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libwnck/libwnck.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "macmenu-tslist.h" // const char* TS_LIST_DEFAULT

#ifdef FOR_XFCE
// begin FOR_XFCE
#include <libxfce4panel/xfce-panel-convenience.h>
#include <libxfce4panel/xfce-panel-plugin.h>
typedef XfcePanelPlugin AppletType;
const gchar* MAIN_LABEL_TEXT = "Xfce";
#elif FOR_GNOME
// begin FOR_GNOME
#include <gconf/gconf-client.h>
#include <panel-applet.h>
typedef PanelApplet AppletType;
const gchar* MAIN_LABEL_TEXT = "GNOME";
#else
// no FOR_GNOME or FOR_XFCE, error
#error "Please specify whether to build GNOME panel applet or Xfce panel plugin"
#endif

const char* TS_LIST_FILENAME = ".macmenu-tslist";
const int MAX_LABEL_WIDTH_N_CHARS = 15;
const int SHORTCUT_SPACING = 8;

typedef struct {
  GtkSocket* socket;
  gint x;
  gint y;
  gint width;
  gint height;
  gboolean removed;
  gulong destroy_handler_id;
} MacMenuEntry;

typedef struct {
  AppletType* applet;
  // Core Data
  WnckScreen* screen;
  MacMenuEntry * current_entry;
  GHashTable* mbars_scks; /*elements are MacMenuEntry s*/
  gboolean hide_label;
  // Widgets
  GtkBox* basebox;
  GtkNotebook* notebook;
  GtkLabel* label;
  GtkWidget* label_space;
  GtkButton* button;
  GtkWidget* mainsck;
  GtkWidget* dummysck;
  GtkLayout* fixed_layout;
  GtkButton* backward;
  GtkButton* forward;
  // Title subtitle hash
  time_t ts_mtime;
  GHashTable* title_subs;
#ifdef FOR_GNOME
  GConfClient* gconf;
#endif
  struct {
     gulong desktop_active_window_changed; 
     gulong desktop_window_opened;
  } handler_id;
} MacMenu;


static int handle_x_error (Display* display,
                           XErrorEvent* error)
{
  printf("Caught stupid X error, ignore it...");
  return 0;
}

// don't use wnck's, it would fail if main window isn't shown yet.
static Window get_transient_for(Window window)
{
  Window parent = 0;
  Window* w = NULL;
  Atom type;
  int format;
  gulong nitems;
  gulong bytes_after;
  if (XGetWindowProperty (gdk_display, window,
                          XInternAtom(gdk_display, "WM_TRANSIENT_FOR", FALSE),
                          0, G_MAXLONG, FALSE, XA_WINDOW,
                          &type, &format, &nitems, &bytes_after,
                          (guchar **) &w) == Success
      && w != NULL)
  {
    parent = *w;
    XFree (w);
  }
  return parent;
}

static gboolean is_menubar(WnckWindow* window)
{
  gboolean ret = FALSE;

  if (wnck_window_get_window_type(window) != WNCK_WINDOW_DOCK)
    ret = FALSE;
  else if (strcmp(wnck_window_get_name(window), "GTK MENUBAR") == 0)
    ret = TRUE;
  else
  {
    Atom type;
    int format;
    gulong nitems;
    gulong bytes_after;
    Atom *data;

    if (XGetWindowProperty(gdk_display,
                           wnck_window_get_xid(window),
                           XInternAtom(gdk_display, "_NET_WM_WINDOW_TYPE", FALSE),
                           0, G_MAXLONG,
                           False, XA_ATOM, &type, &format, &nitems,
                           &bytes_after, (void*) &data) == Success
        && data != NULL)
    {
      if (data[0] == XInternAtom(gdk_display, "_KDE_NET_WM_WINDOW_TYPE_TOPMENU", FALSE))
        ret = TRUE;
      XFree (data);
    }
  }
  return ret;
}

static void find_mbar_by_mwin(gpointer key,
                              gpointer value,
                              gpointer user_data)
{
  Window mbar = (Window) key;
  Window mwin = get_transient_for(mbar);
  long* inout = (long*) user_data;
  if (mwin && mwin == inout[0])
    inout[1] = mbar;
}

static void find_mbar_by_sck(gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  Window mbar = (Window) key;
  GtkWidget* sck = value?((MacMenuEntry*)value)->socket:NULL;
  long* inout = (long*) user_data;
  if (sck && sck == (GtkWidget*) inout[0])
    inout[1] = mbar;
}

static void socket_destroyed(GtkWidget* sck, MacMenu* mmb)
{
  long inout[2] = {0, 0};
  inout[0] = (long) sck;
  g_hash_table_foreach(mmb->mbars_scks, find_mbar_by_sck, inout);
  if (inout[1]){
    g_hash_table_remove(mmb->mbars_scks, (gpointer) inout[1]);
  }
}

static void add_menubar(MacMenu* mmb, WnckWindow* mbarwin)
{
  Window mbar = wnck_window_get_xid(mbarwin);
  GtkWidget* sck = gtk_socket_new();
  gint t1, t2;
  MacMenuEntry * entry = g_new(MacMenuEntry, 1);
  wnck_window_get_geometry(mbarwin, &t1, &t2, &entry->width, &entry->height);
  entry->destroy_handler_id = g_signal_connect(sck, "destroy", G_CALLBACK(socket_destroyed), mmb);
  gtk_notebook_append_page(mmb->notebook, GTK_WIDGET(sck), NULL);
  gtk_socket_steal(GTK_SOCKET(sck), mbar);
  gtk_widget_show_all(sck);
  entry->socket = GTK_SOCKET(sck);
  entry->x = 0;
  entry->y = 0;
  entry->removed = FALSE;
  g_printerr("new menubar added\n");
  g_hash_table_insert(mmb->mbars_scks, (gpointer) mbar, entry);
}

static void update_title_substitute_table(MacMenu* mmb)
{
  gchar* ts_list_path = g_build_filename(g_get_home_dir(), TS_LIST_FILENAME, NULL);
  struct stat sbuf;
  if (g_stat(ts_list_path, &sbuf))
  {
    FILE* nf = g_fopen(ts_list_path, "w");
    if (nf == NULL)
      fprintf(stderr, "Unable to create %s\n", ts_list_path);
    else
    {
      fwrite(TS_LIST_DEFAULT, 1, strlen(TS_LIST_DEFAULT), nf);
      sbuf.st_mtime = time(NULL);
      fclose(nf);
      fprintf(stdout, "New %s created\n", ts_list_path);
    }
  }
  // update if the file has been modified
  if (sbuf.st_mtime > mmb->ts_mtime)
  {
    GIOChannel* ioc = g_io_channel_new_file(ts_list_path, "r", NULL);
    if (ioc == NULL)
      fprintf(stderr, "Unable to open %s for reading list after successful stat()\n", ts_list_path);
    else
    {
      g_hash_table_remove_all(mmb->title_subs);
      gsize line_len, line_term;
      gchar* line_str;
      while (g_io_channel_read_line(ioc, &line_str, &line_len, &line_term,
                                    NULL) == G_IO_STATUS_NORMAL)
      {
        char* sep_pos;
        if (line_str != NULL && (sep_pos = rindex(line_str, '=')) != NULL)
        {
          gchar* key = line_str;
          gchar* value = sep_pos + 1;
          sep_pos[0] = 0;
          line_str[line_term] = 0;
          g_hash_table_insert(mmb->title_subs, key, value);
        }
      }
      g_io_channel_shutdown(ioc, FALSE, NULL);
    }
  }
  mmb->ts_mtime = sbuf.st_mtime;
  g_free(ts_list_path);
}

static const char* get_application_name(WnckWindow* window, MacMenu* mmb)
{
  update_title_substitute_table(mmb);
  const gchar* orig_name = wnck_application_get_name(
    wnck_window_get_application(window));
  const gchar* new_name = g_hash_table_lookup(
    mmb->title_subs, (gpointer) orig_name);
  return (new_name ? new_name: orig_name);



  //printf("[%s]\n", orig_name);
  char* aname = NULL;
  // check vmware
  if (! strcmp(orig_name, "vmware"))
    return g_strdup("VMware");
  // check epiphany
  else if (! strcmp(orig_name, "Web Browser"))
    return g_strdup("Epiphany");
  // check evince
  else if (! strcmp(orig_name, "Evince Document Viewer"))
    return g_strdup("Evince");
  // suse's control center?
  else if (! strcmp(orig_name, "Gnome Control Center"))
    return g_strdup("Control Center");
  // gnome control center
  else if (! strcmp(orig_name, "control-center"))
    return g_strdup("Control Center");
  else if (! strcmp(orig_name, "file-managment-properties"))
    return g_strdup("File Management");
  else if (! strcmp(orig_name, "gcin-setup"))
    return g_strdup("Gcin Setup");
  // other control center parts
  else if (! strncmp(orig_name, "gnome-", 6))
  {
    aname = g_strdup(orig_name+6);
    for (int i=0; i<strlen(aname); i++)
    {
      if (aname[i] == '-') aname[i] = ' ';
    }
  }
  // mono apps
  else if (! strncmp(orig_name, "/opt/", 5)
           || ! strncmp(orig_name, "/usr/", 5))
  {
    aname = g_strdup(rindex(orig_name, '/')+1);
    if (strlen(aname) > 0)
    {
      char* dot = strstr(aname, ".exe");
      if (dot) *dot = 0;
    }
    else
    {
      g_free(aname);
      aname = g_strdup("Mono");
    }
  }
  else
    aname = g_strdup(orig_name);

  gboolean has_upper = FALSE;
  for (int i=0; i<strlen(aname); i++)
  {
    if (aname[i] >= 'A' && aname[i] <= 'Z')
    {
      has_upper = TRUE;
      break;
    }
  }
  if (!has_upper)
  {
    for (int i=0; i<strlen(aname); i++)
    {
      if ((aname[i] >= 'a' && aname[i] <= 'z')
          && (i == 0 || aname[i-1] == ' '))
        aname[i] -= 32;
    }
    char* ui = strstr(aname, "Ui");
    if (ui && (ui[2] == 0 || ui[2] == ' '))
      ui[1] = 'I';
    char* io = strstr(aname, "Io");
    if (io && (io[2] == 0 || io[2] == ' '))
      io[1] = 'O';
    char* at = strstr(aname, "At");
    if (at && (at[2] == 0 || at[2] == ' '))
      at[1] = 'T';
  }
  return aname;
}
static void toggle_forward_backward(MacMenu * mmb){
  if(mmb->current_entry){
    if(mmb->current_entry->x>=0){
      gtk_widget_hide(GTK_WIDGET(mmb->forward));
    }else{
      gtk_widget_show(GTK_WIDGET(mmb->forward));
    }
    if(GTK_WIDGET(mmb->fixed_layout)->allocation.width>mmb->current_entry->width+mmb->current_entry->x){
      gtk_widget_hide(GTK_WIDGET(mmb->backward));
    }else{
      gtk_widget_show(GTK_WIDGET(mmb->backward));
    }
  } else{
      gtk_widget_hide(GTK_WIDGET(mmb->forward));
      gtk_widget_hide(GTK_WIDGET(mmb->backward));
  } 
}
static void desktop_active_window_changed(WnckScreen* screen, WnckWindow *previous_window, MacMenu* mmb)
{
  WnckWindow* awin = wnck_screen_get_active_window(screen);
  GtkWidget* sck = NULL;
  mmb->current_entry = NULL;
  if (awin != NULL && wnck_window_get_window_type(awin) != WNCK_WINDOW_DESKTOP)
  {
    long inout[2] = {0, 0};
    inout[0] = wnck_window_get_xid(awin);
    if (inout[0])
    {
      g_hash_table_foreach(mmb->mbars_scks, find_mbar_by_mwin, inout);
      if (inout[1]){
        mmb->current_entry = g_hash_table_lookup(mmb->mbars_scks, (gpointer) inout[1]);
        sck = GTK_WIDGET(mmb->current_entry->socket);
      }
    }
    if (mmb->current_entry == NULL || mmb->current_entry->removed==TRUE)
    {
      sck = mmb->dummysck;
      gtk_label_set_max_width_chars(mmb->label, MAX_LABEL_WIDTH_N_CHARS /** 10*/);
    }
    else
    {
      gtk_label_set_max_width_chars(mmb->label, MAX_LABEL_WIDTH_N_CHARS);
    }
    gtk_label_set_text(mmb->label, get_application_name(awin, mmb));
  }
  else
  {
    sck = mmb->mainsck;
    gtk_label_set_max_width_chars(mmb->label, MAX_LABEL_WIDTH_N_CHARS * 10);
    gtk_label_set_text(mmb->label, MAIN_LABEL_TEXT);
  }

  gtk_notebook_set_current_page(
    mmb->notebook,
    gtk_notebook_page_num(mmb->notebook, sck)
  );
  if(mmb->current_entry){
    GtkAllocation al;
    gchar * buf;
    al.width = mmb->current_entry->width;
    al.height = GTK_WIDGET(mmb->fixed_layout)->allocation.height+3;
/*+3 is to make the clickable area fills the entire control vertically, related to the test case on gnome bugzilla*/
    gtk_widget_set_size_request(GTK_WIDGET(mmb->notebook),al.width, al.height);

    mmb->current_entry->y = (GTK_WIDGET(mmb->fixed_layout)->allocation.height - al.height) /2;
    gtk_layout_move(mmb->fixed_layout, GTK_WIDGET(mmb->notebook), 
      mmb->current_entry->x, mmb->current_entry->y);
  }
  toggle_forward_backward(mmb);
}

static void desktop_window_opened(WnckScreen* screen, WnckWindow* window, MacMenu* mmb)
{
  if (is_menubar(window))
    add_menubar(mmb, window);
}

static void add_all(MacMenu* mmb)
{
  GList* windows = wnck_screen_get_windows(mmb->screen);
  GList* node = windows;
  while (node != NULL) {
    WnckWindow* wnckwin = (WnckWindow*) node->data;
    if (is_menubar(wnckwin))
      add_menubar(mmb, wnckwin);
    node = node->next;
  }
}

static void connect_desktop_signals(MacMenu * mmb){
  mmb->handler_id.desktop_active_window_changed =
		g_signal_connect(mmb->screen, "active-window-changed",
                   G_CALLBACK(desktop_active_window_changed), mmb);
  mmb->handler_id.desktop_window_opened =
		g_signal_connect(mmb->screen, "window-opened",
                   G_CALLBACK(desktop_window_opened), mmb);
}
static void disconnect_desktop_signals(MacMenu * mmb){
  g_signal_handler_disconnect(mmb->screen, 
		mmb->handler_id.desktop_active_window_changed);
  g_signal_handler_disconnect(mmb->screen, 
		mmb->handler_id.desktop_window_opened);
}
static void macmenu_free_data(AppletType *applet, MacMenu* mmb)
{
#ifdef FOR_GNOME
  g_object_unref(mmb->gconf);
#endif
  //finalize_mainsck(mmb);
  g_hash_table_destroy(mmb->mbars_scks);
  g_hash_table_destroy(mmb->title_subs);
  g_slice_free(MacMenu, mmb);
}
static void macmenu_destroy(AppletType * applet, MacMenu* mmb){
  disconnect_desktop_signals(mmb);
  macmenu_free_data(applet, mmb);
}

static void macmenu_set_size(AppletType *applet, int size, MacMenu* mmb)
{
  gtk_widget_set_size_request(GTK_WIDGET(applet), 0, size);
}

#ifdef FOR_XFCE
static void macmenu_load_from_rc(MacMenu* mmb)
{
  char* file;
  if (!(file = xfce_panel_plugin_lookup_rc_file(mmb->applet))) return;
  XfceRc* rc = xfce_rc_simple_open(file, TRUE);
  g_free(file);
  if (!rc) return;

  mmb->hide_label = xfce_rc_read_bool_entry(rc, "HideLabel", FALSE);

  xfce_rc_close (rc);
}

static void macmenu_write_rc(MacMenu* mmb)
{
  char* file;
  if (!(file = xfce_panel_plugin_save_location(mmb->applet, TRUE))) return;
  unlink(file);
  XfceRc* rc = xfce_rc_simple_open(file, FALSE);
  g_free(file);
  if (!rc) return;

  xfce_rc_write_bool_entry(rc, "HideLabel", mmb->hide_label);

  xfce_rc_close (rc);
}

static void on_label_toggled(GtkCheckMenuItem *checkmenuitem, MacMenu* mmb)
{
  mmb->hide_label = gtk_check_menu_item_get_active(checkmenuitem);
  if (mmb->hide_label)
  {
    gtk_widget_hide(GTK_WIDGET(mmb->label));
    gtk_widget_hide(GTK_WIDGET(mmb->label_space));
  }
  else
  {
    gtk_widget_show(GTK_WIDGET(mmb->label));
    gtk_widget_show(GTK_WIDGET(mmb->label_space));
  }
  macmenu_write_rc(mmb);
}

#elif FOR_GNOME
static void macmenu_load_from_gconf(MacMenu* mmb)
{
  gchar* prefkey = panel_applet_get_preferences_key(mmb->applet);
  gchar* fullkey = g_strdup_printf("%s/%s", prefkey, "hide_label");
  mmb->hide_label = gconf_client_get_bool(mmb->gconf, fullkey, NULL);
  g_free(prefkey);
  g_free(fullkey);
}

static void macmenu_write_gconf(MacMenu* mmb)
{
  gchar* prefkey = panel_applet_get_preferences_key(mmb->applet);
  gchar* fullkey = g_strdup_printf("%s/%s", prefkey, "hide_label");
  gconf_client_set_bool(mmb->gconf, fullkey, mmb->hide_label, NULL);
  g_free(prefkey);
  g_free(fullkey);
}

static void on_label_toggled(BonoboUIComponent *uic, const gchar *path,
                             Bonobo_UIComponent_EventType type, const gchar *state,
                             MacMenu *mmb)
{
  BonoboUIComponent* popup_component = panel_applet_get_popup_component(mmb->applet);
  gchar* r = bonobo_ui_component_get_prop(
    popup_component, "/commands/hide_label", "state", NULL);
  mmb->hide_label = (strcmp(r, "1") == 0 ? TRUE: FALSE);
  g_free(r);
  if (mmb->hide_label)
  {
    gtk_widget_hide(GTK_WIDGET(mmb->label));
    gtk_widget_hide(GTK_WIDGET(mmb->label_space));
  }
  else
  {
    gtk_widget_show(GTK_WIDGET(mmb->label));
    gtk_widget_show(GTK_WIDGET(mmb->label_space));
  }
  macmenu_write_gconf(mmb);
}

static void on_button_pressed(GtkButton* button, MacMenu* mmb)
{
  gboolean unused;
  g_signal_emit_by_name(mmb->applet, "popup_menu", &unused);
  return;
}

static void on_scrollmenu_pressed(GtkButton* button, MacMenu* mmb)
{
  gboolean unused;
  if(mmb->current_entry){ 
/*Read in the new value, in case gtk will modify them*/
	  GValue * propx = g_value_init(g_new0(GValue, 1), G_TYPE_INT);
	  GValue * propy = g_value_init(g_new0(GValue, 1), G_TYPE_INT);
	  gint x, y;
	  gtk_container_child_get_property(GTK_CONTAINER(mmb->fixed_layout),
		GTK_WIDGET(mmb->notebook), "x", propx);
	  gtk_container_child_get_property(GTK_CONTAINER(mmb->fixed_layout),
		GTK_WIDGET(mmb->notebook), "y", propy);
	  x = g_value_get_int(propx);
	  y = g_value_get_int(propy);
	  if(button == mmb->forward){
		x+=40;
	  }
	  if(button == mmb->backward){
		x-=40;
	  }
	  gtk_layout_move(mmb->fixed_layout, GTK_WIDGET(mmb->notebook), x, y);
	  mmb->current_entry->x = x;
	  mmb->current_entry->y = y;
	  g_free(propx);
	  g_free(propy);
      toggle_forward_backward(mmb);
  }
  return;
}
#endif // FOR_GNOME

static void free_entries(MacMenuEntry * entry){
  g_signal_handler_disconnect(entry->socket, entry->destroy_handler_id);
  gtk_widget_destroy(entry->socket);
  g_free(entry);
}
static void macmenu_construct(AppletType* applet)
{
  MacMenu *mmb = g_slice_new0(MacMenu);
  mmb->applet = applet;
  mmb->screen = wnck_screen_get(gdk_screen_get_number(
      gtk_widget_get_screen(GTK_WIDGET(applet))
      ));
  mmb->mbars_scks = g_hash_table_new_full(g_direct_hash, 
     g_direct_equal, NULL, free_entries);
  mmb->title_subs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  mmb->ts_mtime = 0;
  mmb->current_entry = NULL;

  mmb->basebox = GTK_BOX(gtk_hbox_new(FALSE, 0));
  gtk_container_set_border_width(GTK_CONTAINER(mmb->basebox), 0);
  gtk_container_add(GTK_CONTAINER(applet), GTK_WIDGET(mmb->basebox));

  mmb->label = GTK_LABEL(gtk_label_new(MAIN_LABEL_TEXT));
  PangoAttrList *pattr = pango_attr_list_new();
  PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
  pa->start_index = 0; pa->end_index = 1024;
  pango_attr_list_insert(pattr, pa);
  gtk_label_set_attributes(mmb->label, pattr);
  pango_attr_list_unref(pattr);
  gtk_label_set_ellipsize(mmb->label, PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars(mmb->label, MAX_LABEL_WIDTH_N_CHARS * 10);
  gtk_label_set_single_line_mode(mmb->label, TRUE);
  gtk_box_pack_start(mmb->basebox, GTK_WIDGET(mmb->label), FALSE, FALSE, 0);

  mmb->label_space = gtk_event_box_new();
  gtk_widget_set_size_request(mmb->label_space, 8, 1);
  gtk_box_pack_start(mmb->basebox, mmb->label_space, FALSE, FALSE, 0);

  mmb->forward = GTK_BUTTON(gtk_button_new_with_label(">"));
  gtk_button_set_relief(mmb->forward, GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(mmb->forward), FALSE);
  gtk_box_pack_start(mmb->basebox, GTK_WIDGET(mmb->forward),
                     FALSE, FALSE, 0);

  mmb->fixed_layout = GTK_FIXED(gtk_layout_new(NULL,NULL));
  gtk_box_pack_start(mmb->basebox, GTK_WIDGET(mmb->fixed_layout), TRUE, TRUE, 0);

  mmb->notebook = GTK_NOTEBOOK(gtk_notebook_new());
  gtk_notebook_set_show_tabs(mmb->notebook, FALSE);
  gtk_container_set_border_width(mmb->notebook, 0);
  gtk_notebook_set_show_border(mmb->notebook, FALSE);
  gtk_layout_put(mmb->fixed_layout, GTK_WIDGET(mmb->notebook), 0, 0);

  mmb->dummysck = gtk_hbox_new(FALSE, 0);
  gtk_notebook_append_page(mmb->notebook, mmb->dummysck, NULL);

  mmb->mainsck = gtk_hbox_new(FALSE, SHORTCUT_SPACING);
  gtk_notebook_append_page(mmb->notebook, mmb->mainsck, NULL);
  //initialize_mainsck(mmb);

  mmb->backward = GTK_BUTTON(gtk_button_new_with_label("<"));
  gtk_button_set_relief(mmb->backward, GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(mmb->backward), FALSE);
  gtk_box_pack_start(mmb->basebox, GTK_WIDGET(mmb->backward),
                     FALSE, FALSE, 0);

  mmb->button = GTK_BUTTON(gtk_button_new());
  gtk_button_set_relief(mmb->button, GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(mmb->button), FALSE);
  gtk_box_pack_start(mmb->basebox, GTK_WIDGET(mmb->button),
                     FALSE, FALSE, 0);

  connect_desktop_signals(mmb);
#ifdef FOR_XFCE
  g_signal_connect(applet, "free-data",
                   G_CALLBACK(macmenu_free_data), mmb);
  g_signal_connect(applet, "size-changed",
                   G_CALLBACK(macmenu_set_size), mmb);
  // setup panel applet
  gtk_widget_show_all(GTK_WIDGET(mmb->basebox));
  xfce_panel_plugin_add_action_widget (applet, GTK_WIDGET(mmb->button));
  xfce_panel_plugin_set_expand(applet, TRUE);
  macmenu_set_size(applet, xfce_panel_plugin_get_size(applet), mmb);
  // load config, set popup
  macmenu_load_from_rc(mmb);
  GtkWidget* label_toggle = gtk_check_menu_item_new_with_label("Hide Label");
  g_signal_connect(label_toggle, "toggled", G_CALLBACK(on_label_toggled), mmb);
  gtk_widget_show(label_toggle);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(label_toggle), mmb->hide_label);
  gtk_check_menu_item_toggled(GTK_CHECK_MENU_ITEM(label_toggle));
  xfce_panel_plugin_menu_insert_item(mmb->applet, GTK_MENU_ITEM(label_toggle));

#elif FOR_GNOME
  g_signal_connect(applet, "destroy", G_CALLBACK(macmenu_destroy), mmb);
  // setup panel applet
  gtk_widget_show_all(GTK_WIDGET(applet));
  g_signal_connect(GTK_WIDGET(mmb->button), "pressed",
                   G_CALLBACK(on_button_pressed), mmb);
  g_signal_connect(GTK_WIDGET(mmb->forward), "pressed",
                   G_CALLBACK(on_scrollmenu_pressed), mmb);
  g_signal_connect(GTK_WIDGET(mmb->backward), "pressed",
                   G_CALLBACK(on_scrollmenu_pressed), mmb);

  panel_applet_set_flags (applet, PANEL_APPLET_EXPAND_MAJOR | PANEL_APPLET_EXPAND_MINOR); /*fix the vertical align problem*/
//  macmenu_set_size(applet, panel_applet_get_size(applet), mmb); no need to set size because the fix above
  // load config, set popup
  mmb->gconf = gconf_client_get_default();
  macmenu_load_from_gconf(mmb);
  macmenu_write_gconf(mmb);
  static const char toggle_menu_xml [] =
   "<popup name=\"button3\">\n"
   "   <menuitem name=\"hide_label\" "
   "             verb=\"hide_label\" "
   "           _label=\"_Hide Label\"\n"
   "             type=\"toggle\"/>\n"
   "</popup>\n";
  static const BonoboUIVerb toggle_menu_verbs[] =
  {
    BONOBO_UI_VERB_END
  };
  panel_applet_setup_menu(mmb->applet, toggle_menu_xml, toggle_menu_verbs, mmb);
  BonoboUIComponent* popup_component = panel_applet_get_popup_component(mmb->applet);
  bonobo_ui_component_set_prop(popup_component, "/commands/hide_label",
                               "state", mmb->hide_label ? "1": "0", NULL);
  bonobo_ui_component_add_listener(panel_applet_get_popup_component(mmb->applet),
                                   "hide_label",
                                   (BonoboUIListenerFn) on_label_toggled,
                                   mmb);
  on_label_toggled(NULL, NULL, 0, NULL, mmb);
#endif // FOR_XFCE/FOR_GNOME

  add_all(mmb);
  XSetErrorHandler(handle_x_error);
}

#ifdef FOR_XFCE
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL (macmenu_construct);

#elif FOR_GNOME
static gboolean macmenu_applet_factory (PanelApplet *applet,
                                        const gchar *iid,
                                        gpointer data)
{
  if (!strcmp (iid, "OAFIID:GNOME_MacMenuApplet"))
  {
    macmenu_construct (applet);
    return TRUE;
  }
  else
    return FALSE;
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:GNOME_MacMenuApplet_Factory",
			     PANEL_TYPE_APPLET,
			     "macmenu",
			     "0",
			     macmenu_applet_factory,
			     NULL)
#endif // FOR_XFCE/FOR_GNOME
