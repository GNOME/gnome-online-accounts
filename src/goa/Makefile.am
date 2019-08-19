-include $(INTROSPECTION_MAKEFILE)

NULL =
BUILT_SOURCES =
CLEANFILES =
DISTCLEANFILES =
EXTRA_DIST =
INTROSPECTION_GIRS =

AM_CPPFLAGS = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
	-DG_LOG_DOMAIN=\"Goa\"					\
	-DGOA_COMPILATION					\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	$(WARN_CFLAGS)						\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

DISTCLEANFILES += goaconfig-stamp goaconfig.h
BUILT_SOURCES += goaconfig-stamp
configexecincludedir = $(libdir)/goa-1.0/include
nodist_configexecinclude_HEADERS = goaconfig.h
goaconfig-stamp: ../../config.status
	$(AM_V_GEN) cd $(top_builddir) && \
	  $(SHELL) ./config.status src/goa/goaconfig.h
	@touch goaconfig-stamp

# ----------------------------------------------------------------------------------------------------

dbus_built_sources = goa-generated.c goa-generated.h goa-generated-doc-org.gnome.OnlineAccounts.Account.xml
BUILT_SOURCES += $(dbus_built_sources)

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/dbus-interfaces.xml
	$(AM_V_GEN) gdbus-codegen							                \
		--interface-prefix org.gnome.OnlineAccounts.                          	\
		--c-generate-autocleanup all						\
		--c-namespace Goa							\
		--c-generate-object-manager						\
		--generate-c-code goa-generated                             		\
		--generate-docbook goa-generated-doc					\
		$(top_srcdir)/data/dbus-interfaces.xml					\
		$(NULL)

# ----------------------------------------------------------------------------------------------------

goaenumtypes.h: goaenums.h goaenumtypes.h.template
	$(AM_V_GEN) ( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template goaenumtypes.h.template goaenums.h ) > 	\
	   goaenumtypes.h.tmp && mv goaenumtypes.h.tmp goaenumtypes.h

goaenumtypes.c: goaenums.h goaenumtypes.h goaenumtypes.c.template
	$(AM_V_GEN) ( top_builddir=`cd $(top_builddir) && pwd`; 						\
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
	goaenums.h						\
	goaenumtypes.h						\
	goaversion.h						\
	goa-generated.h						\
	$(NULL)

libgoa_1_0_la_SOURCES =						\
	goa.h							\
	goaclient.h			goaclient.c		\
	goaerror.h			goaerror.c		\
	goa-generated.h			goa-generated.c		\
	goaenums.h						\
	goaenumtypes.h			goaenumtypes.c		\
	goaversion.h			goaversion.c		\
	$(NULL)

libgoa_1_0_la_CFLAGS =						\
	$(GLIB_CFLAGS) 						\
	$(NULL)

libgoa_1_0_la_LIBADD = 						\
	$(GLIB_LIBS) 						\
	$(NULL)

libgoa_1_0_la_LDFLAGS = 					\
	$(WARN_LDFLAGS)						\
	-version-info $(GOA_LT_VERSION)				\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = goa-1.0.pc

# ----------------------------------------------------------------------------------------------------

if HAVE_INTROSPECTION
Goa-1.0.gir: libgoa-1.0.la
Goa_1_0_gir_INCLUDES = Gio-2.0
Goa_1_0_gir_PACKAGES = glib-2.0 gobject-2.0 gio-2.0
Goa_1_0_gir_EXPORT_PACKAGES = goa-1.0
Goa_1_0_gir_LIBS = goa-1.0
Goa_1_0_gir_FILES = $(libgoa_1_0_la_HEADERS) $(libgoa_1_0_la_SOURCES)
Goa_1_0_gir_CFLAGS = $(AM_CPPFLAGS) $(libgoa_1_0_la_CFLAGS)
Goa_1_0_gir_SCANNERFLAGS = $(WARN_SCANNERFLAGS) --c-include='goa/goa.h'
INTROSPECTION_GIRS += Goa-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = Goa-1.0.gir

typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = Goa-1.0.typelib

CLEANFILES += $(gir_DATA) $(typelibs_DATA)

if ENABLE_VAPIGEN
-include $(VAPIGEN_MAKEFILE)

goa-1.0.vapi: Goa-1.0.gir Goa-1.0.metadata goa-1.0.deps

VAPIGEN_VAPIS = goa-1.0.vapi

goa_1_0_vapi_DEPS = gio-2.0
goa_1_0_vapi_METADATADIRS = $(srcdir)
goa_1_0_vapi_FILES = Goa-1.0.gir

goa-1.0.deps:
	$(AM_V_GEN) for pkg in $(goa_1_0_vapi_DEPS); do \
		echo $$pkg >> $@; \
	done

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPIGEN_VAPIS) $(VAPIGEN_VAPIS:.vapi=.deps)

CLEANFILES += $(vapi_DATA)

EXTRA_DIST += Goa-1.0.metadata

endif # ENABLE_VAPIGEN

endif # HAVE_INTROSPECTION

# ----------------------------------------------------------------------------------------------------

CLEANFILES += \
	$(dbus_built_sources) \
	$(enum_built_sources) \
	goa-generated-doc-*.xml \
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
