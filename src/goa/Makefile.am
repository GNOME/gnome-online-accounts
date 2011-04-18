
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
	-DGOA_COMPILATION					\
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

dbus_built_sources = goa-generated.c goa-generated.h
BUILT_SOURCES = $(dbus_built_sources)

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/dbus-interfaces.xml
	gdbus-codegen							                \
		--interface-prefix org.gnome.OnlineAccounts.                          	\
		--c-namespace Goa							\
		--c-generate-object-manager						\
		--generate-c-code goa-generated                             		\
		--generate-docbook goa-generated-doc					\
		$(top_srcdir)/data/dbus-interfaces.xml					\
		$(NULL)

# ----------------------------------------------------------------------------------------------------

goaenumtypes.h: goaenums.h goaenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template goaenumtypes.h.template goaenums.h ) > 	\
	   goaenumtypes.h.tmp && mv goaenumtypes.h.tmp goaenumtypes.h

goaenumtypes.c: goaenums.h goaenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template goaenumtypes.c.template goaenums.h ) > 	\
	   goaenumtypes.c.tmp && mv goaenumtypes.c.tmp goaenumtypes.c

enum_built_sources =							\
	goaenumtypes.h			goaenumtypes.c			\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

lib_LTLIBRARIES = libgoa.la

libgoa_ladir = $(includedir)/goa/goa

libgoa_la_HEADERS =						\
	goa.h							\
	goaclient.h						\
	goaerror.h						\
	goatypes.h						\
	goaenums.h						\
	goaenumtypes.h						\
	$(NULL)

libgoa_la_SOURCES =						\
	goa.h							\
	goaclient.h		goaclient.c			\
	goaerror.h		goaerror.c			\
	goa-generated.h		goa-generated.c			\
	goatypes.h						\
	goaenums.h						\
	goaenumtypes.h		goaenumtypes.c			\
	$(NULL)

libgoa_la_CFLAGS =						\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS)						\
	$(GNOME_CONTROL_CENTER_CFLAGS)				\
	$(NULL)

libgoa_la_LIBADD = 						\
	$(GLIB_LIBS) 						\
	$(GTK_LIBS)						\
	$(GNOME_CONTROL_CENTER_LIBS)				\
	$(NULL)

clean-local :
	rm -f *~ goa-generated-doc-*.xml $(dbus_built_sources) $(enum_built_sources)
