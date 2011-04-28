
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
	goaclient.h			goaclient.c		\
	goaerror.h			goaerror.c		\
	goa-generated.h			goa-generated.c		\
	goatypes.h						\
	goaenums.h						\
	goaenumtypes.h			goaenumtypes.c		\
	$(NULL)

libgoa_la_CFLAGS =						\
	-DGOA_COMPILATION					\
	$(GLIB_CFLAGS) 						\
	$(NULL)

libgoa_la_LIBADD = 						\
	$(GLIB_LIBS) 						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

lib_LTLIBRARIES += libgoa-backend.la

libgoa_backend_ladir = $(includedir)/goa/goa

libgoa_backend_la_HEADERS =						\
	goabackend.h							\
	goabackendtypes.h						\
	goabackendprovider.h						\
	$(NULL)

libgoa_backend_la_SOURCES =						\
	goabackend.h							\
	goabackendtypes.h						\
	goabackendprovider.h		goabackendprovider.c		\
	goabackendoauth2provider.h	goabackendoauth2provider.c	\
	goabackendgoogleprovider.h	goabackendgoogleprovider.c	\
	goabackendfacebookprovider.h	goabackendfacebookprovider.c	\
	$(NULL)

libgoa_backend_la_CFLAGS =					\
	-DGOA_BACKEND_COMPILATION				\
	-DGOA_API_IS_SUBJECT_TO_CHANGE				\
	$(WEBKIT_GTK_CFLAGS)					\
	$(JSON_GLIB_CFLAGS)					\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS) 						\
	$(GNOME_KEYRING_CFLAGS)					\
	$(REST_CFLAGS)						\
	$(NULL)

libgoa_backend_la_LIBADD = 					\
	$(WEBKIT_GTK_LIBS)					\
	$(JSON_GLIB_LIBS)					\
	$(GLIB_LIBS) 						\
	$(GTK_LIBS) 						\
	$(GNOME_KEYRING_LIBS)					\
	$(REST_LIBS)						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

clean-local :
	rm -f *~ goa-generated-doc-*.xml $(dbus_built_sources) $(enum_built_sources)
