
NULL =
CLEANFILES =
EXTRA_DIST =

AM_CPPFLAGS = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
	-I$(top_srcdir)/src/goaidentity				\
	-I$(top_builddir)/src/goaidentity			\
	-DG_LOG_DOMAIN=\"GoaBackend\"				\
	-DGOA_BACKEND_COMPILATION				\
	-DGOA_API_IS_SUBJECT_TO_CHANGE				\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-DPACKAGE_WEB_EXTENSIONS_DIR=\""$(libdir)/goa-1.0/web-extensions"\" \
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

goabackendenumtypes.h: goabackendenums.h goabackendenumtypes.h.template
	$(AM_V_GEN) ( top_builddir=`cd $(top_builddir) && pwd`; 					\
	 cd $(srcdir) && glib-mkenums --template goabackendenumtypes.h.template goabackendenums.h ) > 	\
	   goabackendenumtypes.h.tmp && mv goabackendenumtypes.h.tmp goabackendenumtypes.h

goabackendenumtypes.c: goabackendenums.h goabackendenumtypes.c.template goabackendenumtypes.h
	$(AM_V_GEN) ( top_builddir=`cd $(top_builddir) && pwd`; 					\
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
	goabackendenums.h						\
	goabackendenumtypes.h						\
	goaprovider.h							\
	$(NULL)

libgoa_backend_1_0_la_built_sources =						\
	goadleynaservermanager.h		goadleynaservermanager.c	\
	goadleynaservermediadevice.h		goadleynaservermediadevice.c	\
	$(NULL)

nodist_libgoa_backend_1_0_la_SOURCES =					\
	$(libgoa_backend_1_0_la_built_sources)				\
	$(NULL)

libgoa_backend_1_0_la_SOURCES =						\
	goabackend.h							\
	goabackendenums-priv.h						\
	goabackendenumtypes.h		goabackendenumtypes.c		\
	gconstructor.h			goabackendinit.c		\
	goadlnaservermanager.h		goadlnaservermanager.c		\
	goaewsclient.h			goaewsclient.c			\
	goahttpclient.h			goahttpclient.c			\
	goaprovider-priv.h		goaprovider.c			\
	goamailauth.h			goamailauth.c			\
	goaimapauthlogin.h		goaimapauthlogin.c		\
	goarestproxy.h			goarestproxy.c			\
	goasmtpauth.h			goasmtpauth.c			\
	goasouplogger.h			goasouplogger.c			\
	goamailclient.h			goamailclient.c			\
	goaexchangeprovider.h		goaexchangeprovider.c		\
	goaoauthprovider.h		goaoauthprovider.c		\
	goaoauth2provider.h		goaoauth2provider-priv.h	\
	goaoauth2provider-web-extension.h				\
	goaoauth2provider-web-view.h					\
	goaoauth2provider.c						\
	goagoogleprovider.h		goagoogleprovider.c		\
	goafacebookprovider.h		goafacebookprovider.c		\
	goaimapsmtpprovider.h		goaimapsmtpprovider.c		\
	goamediaserverprovider.h	goamediaserverprovider.c	\
	goaowncloudprovider.h		goaowncloudprovider.c		\
	goaflickrprovider.h		goaflickrprovider.c		\
	goafoursquareprovider.h		goafoursquareprovider.c		\
	goawindowsliveprovider.h	goawindowsliveprovider.c	\
	goapocketprovider.h		goapocketprovider.c		\
	goalastfmprovider.h		goalastfmprovider.c		\
	goaobjectskeletonutils.h	goaobjectskeletonutils.c	\
	goautils.h			goautils.c			\
	goawebview.h			goawebview.c			\
	nautilus-floating-bar.h		nautilus-floating-bar.c		\
	$(top_builddir)/src/goaidentity/org.gnome.Identity.c		\
	$(top_srcdir)/src/goaidentity/goaidentitymanagererror.c		\
	$(NULL)

if BUILD_FEDORA
libgoa_backend_1_0_la_SOURCES +=					\
	goafedoraprovider.h		goafedoraprovider.c
endif

if BUILD_KERBEROS
libgoa_backend_1_0_la_SOURCES +=					\
	goakerberosprovider.h		goakerberosprovider.c
endif

libgoa_backend_1_0_la_CFLAGS =					\
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
	$(WARN_LDFLAGS)						\
	-version-info $(GOA_BACKEND_LT_VERSION)			\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

webextension_LTLIBRARIES = libgoawebextension.la

webextensiondir = $(libdir)/goa-1.0/web-extensions

libgoawebextension_la_SOURCES =						\
	goawebextension.h		goawebextension.c		\
	goawebextensionmain.c						\
	$(NULL)

libgoawebextension_la_CFLAGS =						\
	$(REST_CFLAGS)							\
	$(WEBKIT_GTK_CFLAGS)						\
	$(NULL)

libgoawebextension_la_LIBADD =						\
	libgoa-backend-1.0.la						\
	$(REST_LIBS)							\
	$(WEBKIT_GTK_LIBS)						\
	$(NULL)

libgoawebextension_la_LDFLAGS =						\
	-avoid-version							\
	-module								\
	-no-undefined							\
	$(NULL)

# Force installation order: libgoa-backend-1.0 must be installed first, othwerwise
# libtool will incorrectly relink libgoawebextension.la under parallel make install.
# Requires ugly automake syntax - see http://debbugs.gnu.org/cgi/bugreport.cgi?bug=7328

installwebextensionLTLIBRARIES = install-webextensionLTLIBRARIES
$(installwebextensionLTLIBRARIES): install-libLTLIBRARIES

# ----------------------------------------------------------------------------------------------------

BUILT_SOURCES =								\
	$(libgoa_backend_1_0_la_built_sources)				\
	$(NULL)

EXTRA_DIST +=								\
	goadleynaservermanager.xml					\
	goadleynaservermediadevice.xml					\
	$(NULL)

goadleynaservermanager.h goadleynaservermanager.c: goadleynaservermanager.xml
	$(AM_V_GEN)gdbus-codegen						\
		--c-generate-autocleanup all					\
		--c-namespace DleynaServer					\
		--generate-c-code goadleynaservermanager			\
		--interface-prefix com.intel.dLeynaServer.			\
		$<

goadleynaservermediadevice.h goadleynaservermediadevice.c: goadleynaservermediadevice.xml
	$(AM_V_GEN)gdbus-codegen								\
		--c-generate-autocleanup all							\
		--c-namespace DleynaServer							\
		--generate-c-code goadleynaservermediadevice					\
		--interface-prefix com.intel.dLeynaServer.					\
		$<

# ----------------------------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = goa-backend-1.0.pc

# ----------------------------------------------------------------------------------------------------

CLEANFILES += \
	$(BUILT_SOURCES) \
	$(enum_built_sources) \
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
