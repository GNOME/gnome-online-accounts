
NULL =
CLEANFILES =
EXTRA_DIST =

INCLUDES = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
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

dbus_built_sources = goa-generated.c goa-generated.h goa-generated-doc-org.gnome.OnlineAccounts.Account.xml
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

goaenumtypes.c: goaenums.h goaenumtypes.h goaenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template goaenumtypes.c.template goaenums.h ) > 	\
	   goaenumtypes.c.tmp && mv goaenumtypes.c.tmp goaenumtypes.c

enum_built_sources =							\
	goaenumtypes.h			goaenumtypes.c			\
	$(NULL)

EXTRA_DIST += goaenumtypes.h.template goaenumtypes.c.template

# ----------------------------------------------------------------------------------------------------

lib_LTLIBRARIES = libgoa-1.0.la

libgoa_1_0_ladir = $(includedir)/goa-1.0/goa

libgoa_1_0_la_HEADERS =						\
	goa.h							\
	goaclient.h						\
	goaerror.h						\
	goatypes.h						\
	goaenums.h						\
	goaenumtypes.h						\
	goa-generated.h						\
	$(NULL)

libgoa_1_0_la_SOURCES =						\
	goa.h							\
	goaclient.h			goaclient.c		\
	goaerror.h			goaerror.c		\
	goa-generated.h			goa-generated.c		\
	goatypes.h						\
	goaenums.h						\
	goaenumtypes.h			goaenumtypes.c		\
	$(NULL)

libgoa_1_0_la_CPPFLAGS =					\
	-DG_LOG_DOMAIN=\"Goa\"					\
	$(NULL)

libgoa_1_0_la_CFLAGS =						\
	-DGOA_COMPILATION					\
	$(GLIB_CFLAGS) 						\
	$(NULL)

libgoa_1_0_la_LIBADD = 						\
	$(GLIB_LIBS) 						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = goa-1.0.pc

# ----------------------------------------------------------------------------------------------------

if HAVE_INTROSPECTION
girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = Goa-1.0.gir

typelibsdir = $(INTROSPECTION_TYPELIBDIR)
typelibs_DATA = Goa-1.0.typelib

Goa-1.0.gir: $(libgoa_1_0_la_SOURCES)
	$(INTROSPECTION_SCANNER) -v 				\
		--warn-all					\
		--namespace Goa 				\
		--nsversion=1.0 				\
		--include=Gio-2.0 				\
		--library=goa-1.0	 			\
		--output $@ 					\
		--pkg=glib-2.0 					\
		--pkg=gobject-2.0 				\
		--pkg=gio-2.0 					\
		--pkg-export=goa-1.0				\
		--libtool=$(top_builddir)/libtool		\
		--c-include='goa/goa.h'				\
                -I$(top_srcdir)/src	 			\
                -I$(top_srcdir)/src/goa	 			\
                -I$(top_builddir)/src	 			\
                -I$(top_builddir)/src/goa 			\
	        -DGOA_COMPILATION                   		\
		$^						\
		$(NULL)

Goa-1.0.gir: libgoa-1.0.la $(INTROSPECTION_SCANNER) Makefile.am


Goa-1.0.typelib: Goa-1.0.gir $(INTROSPECTION_COMPILER)
	$(INTROSPECTION_COMPILER) $< -o $@

CLEANFILES += $(gir_DATA) $(typelibs_DATA)

endif # HAVE_INTROSPECTION

# ----------------------------------------------------------------------------------------------------

clean-local :
	rm -f *~ goa-generated-doc-*.xml $(dbus_built_sources) $(enum_built_sources)

-include $(top_srcdir)/git.mk
