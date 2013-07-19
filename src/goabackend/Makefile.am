
NULL =
CLEANFILES =
EXTRA_DIST =

INCLUDES = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
	-I$(top_srcdir)/src/goaidentity				\
	-I$(top_builddir)/src/goaidentity			\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

goabackendenumtypes.h: goabackendenums.h goabackendenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; 							\
	 cd $(srcdir) && glib-mkenums --template goabackendenumtypes.h.template goabackendenums.h ) > 	\
	   goabackendenumtypes.h.tmp && mv goabackendenumtypes.h.tmp goabackendenumtypes.h

goabackendenumtypes.c: goabackendenums.h goabackendenumtypes.c.template goabackendenumtypes.h
	( top_builddir=`cd $(top_builddir) && pwd`; 							\
	 cd $(srcdir) && glib-mkenums --template goabackendenumtypes.c.template goabackendenums.h ) > 	\
	   goabackendenumtypes.c.tmp && mv goabackendenumtypes.c.tmp goabackendenumtypes.c

enum_built_sources =							\
	goabackendenumtypes.h		goabackendenumtypes.c		\
	$(NULL)

EXTRA_DIST += goabackendenumtypes.h.template goabackendenumtypes.c.template

# ----------------------------------------------------------------------------------------------------

lib_LTLIBRARIES = libgoa-backend-1.0.la

libgoa_backend_1_0_ladir = $(includedir)/goa-1.0/goabackend

libgoa_backend_1_0_la_HEADERS =						\
	goabackend.h							\
	goabackendtypes.h						\
	goabackendenums.h						\
	goabackendenumtypes.h						\
	goaprovider.h							\
	$(NULL)

libgoa_backend_1_0_la_SOURCES =						\
	goabackend.h							\
	goabackendtypes.h						\
	goabackendenums-priv.h						\
	goabackendenumtypes.h		goabackendenumtypes.c		\
	goaeditablelabel.h		goaeditablelabel.c		\
	goaewsclient.h			goaewsclient.c			\
	goahttpclient.h			goahttpclient.c			\
	goaprovider-priv.h		goaprovider.c			\
	goaproviderfactory.h		goaproviderfactory.c		\
	goamailauth.h			goamailauth.c			\
	goaimapauthlogin.h		goaimapauthlogin.c		\
	goasmtpauth.h			goasmtpauth.c			\
	goamailclient.h			goamailclient.c			\
	goaexchangeprovider.h		goaexchangeprovider.c		\
	goalogging.h			goalogging.c			\
	goaoauthprovider.h		goaoauthprovider.c		\
	goaoauth2provider.h		goaoauth2provider.c		\
	goagoogleprovider.h		goagoogleprovider.c		\
	goafacebookprovider.h		goafacebookprovider.c		\
	goaimapsmtpprovider.h		goaimapsmtpprovider.c		\
	goaowncloudprovider.h		goaowncloudprovider.c		\
	goayahooprovider.h		goayahooprovider.c		\
	goatwitterprovider.h		goatwitterprovider.c		\
	goaflickrprovider.h		goaflickrprovider.c		\
	goawindowsliveprovider.h	goawindowsliveprovider.c	\
	goatelepathyfactory.h		goatelepathyfactory.c		\
	goatelepathyprovider.h		goatelepathyprovider.c		\
	goapocketprovider.h		goapocketprovider.c		\
	goautils.h			goautils.c			\
	goaspinnerbutton.h		goaspinnerbutton.c		\
	goawebview.h			goawebview.c			\
	nautilus-floating-bar.h		nautilus-floating-bar.c		\
	$(top_builddir)/src/goaidentity/org.gnome.Identity.c		\
	$(top_builddir)/src/goaidentity/org.freedesktop.realmd.c	\
	$(NULL)

if BUILD_KERBEROS
libgoa_backend_1_0_la_SOURCES +=					\
	goakerberosprovider.h		goakerberosprovider.c
endif

libgoa_backend_1_0_la_CPPFLAGS =				\
	-DG_LOG_DOMAIN=\"GoaBackend\"				\
	$(NULL)

libgoa_backend_1_0_la_CFLAGS =					\
	-DGOA_BACKEND_COMPILATION				\
	-DGOA_API_IS_SUBJECT_TO_CHANGE				\
	$(WEBKIT_GTK_CFLAGS)					\
	$(JSON_GLIB_CFLAGS)					\
	$(GCR_CFLAGS) 						\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS) 						\
	$(REST_CFLAGS)						\
	$(SECRET_CFLAGS)					\
	$(LIBSOUP_CFLAGS)					\
	$(LIBXML_CFLAGS)					\
	$(TP_CFLAGS)						\
	$(TPAW_CFLAGS)						\
	$(NULL)

libgoa_backend_1_0_la_LIBADD = 					\
	$(top_builddir)/src/goa/libgoa-1.0.la			\
	$(WEBKIT_GTK_LIBS)					\
	$(JSON_GLIB_LIBS)					\
	$(GCR_LIBS) 						\
	$(GLIB_LIBS) 						\
	$(GTK_LIBS) 						\
	$(REST_LIBS)						\
	$(SECRET_LIBS)						\
	$(LIBSOUP_LIBS)						\
	$(LIBXML_LIBS)						\
	$(TP_LIBS)						\
	$(TPAW_LIBS)						\
	$(NULL)

libgoa_backend_1_0_la_LDFLAGS = 				\
	-version-info $(GOA_BACKEND_LT_VERSION)			\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = goa-backend-1.0.pc

# ----------------------------------------------------------------------------------------------------

CLEANFILES += \
	$(enum_built_sources) \
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
