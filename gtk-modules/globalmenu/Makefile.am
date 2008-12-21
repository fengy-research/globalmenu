NULL = 
SUBDIRS = . tests
STAMP = globalmenu-gnome-private.vapi

BUILT_SOURCES = \
	$(STAMP) \
	$(NULL)

VALASOURCES = \
	module-main.vala \
	globalmenu.vala \
	globalmenu-serializer.vala \
	globalmenu-locator.vala \
	$(NULL)

libglobalmenu_gnomedir = $(GTK_MODULES_DIR)
libglobalmenu_gnome_LTLIBRARIES = \
	libglobalmenu-gnome.la \
	$(NULL)

libglobalmenu_gnome_la_SOURCES = \
	$(VALASOURCES:.vala=.c) \
	$(VALASOURCES:.vala=.h) \
	globalmenu-helper.c \
	dyn-patch-menubar.c \
	dyn-patch-menushell.c \
	dyn-patch.h \
	dyn-patch.c \
	dyn-patch-widget.c \
	$(NULL)

$(STAMP): $(VALASOURCES) bindings.vapi patch.sh
	$(VALAC) -C --basedir=$(top_srcdir) --pkg gmodule-2.0 --pkg gtk+-2.0 --pkg gio-unix-2.0 --library=$(@:.vapi=) --vapidir=. --pkg bindings $(VALASOURCES)
	bash patch.sh
	touch $@
$(STAMP:.vapi=.deps):
	echo > $@

libglobalmenu_gnome_la_LIBADD = \
		$(GLIB_LIBS) \
		$(GTK_LIBS) \
		$(GIO_LIBS) \
		$(NULL)
libglobalmenu_gnome_la_LDFLAGS = \
		-g \
		-version-info 1:0:0 \
		-avoid-version \
		-release 0.7.0 \
		-no-undefined \
		-export-dynamic \
		-module

libglobalmenu_gnome_la_CPPFLAGS = \
	  -I$(top_srcdir) $(GLIB_CFLAGS) $(GTK_CFLAGS) $(GIO_CFLAGS) \
	  -include $(CONFIG_HEADER) \
	  -DG_LOG_DOMAIN=\"GlobalMenu\" \
	  -g

EXTRA_DIST = $(STAMP) $(STAMP:.vapi=.deps)\
	 patch.sh \
	bindings.vapi \
	$(VALASOURCES) \
	$(NULL)
