
NULL =

INCLUDES = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DGOA_API_IS_SUBJECT_TO_CHANGE				\
	-DGOA_BACKEND_API_IS_SUBJECT_TO_CHANGE			\
	$(WARN_CFLAGS)						\
	$(NULL)

ccpanelsdir = $(libdir)/control-center-1/panels
ccpanels_LTLIBRARIES = libgoa-panel.la

libgoa_panel_la_SOURCES =					\
	goapanel.h			goapanel.c		\
	goapanelaccountsmodel.h		goapanelaccountsmodel.c	\
	$(NULL)

libgoa_panel_la_CFLAGS =					\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS)						\
	$(GNOME_CONTROL_CENTER_CFLAGS)				\
	$(NULL)

libgoa_panel_la_LDFLAGS = 			\
	-avoid-version -no-undefined		\
	-export-symbols-regex '^g_io_module_(load|unload)'
	$(NULL)

libgoa_panel_la_LIBADD = 					\
	$(GLIB_LIBS) 						\
	$(GTK_LIBS)						\
	$(GNOME_CONTROL_CENTER_LIBS)				\
	$(WEBKITGTK_LIBS)					\
	$(top_builddir)/src/goa/libgoa-1.0.la			\
	$(top_builddir)/src/goabackend/libgoa-backend-1.0.la	\
	$(NULL)

clean-local :
	rm -f *~
