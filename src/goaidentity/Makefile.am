NULL =
BUILT_SOURCES = $(NULL)
EXTRA_DIST = $(NULL)
CLEANFILES= $(NULL)

AM_CPPFLAGS = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
	-I$(top_srcdir)/src/goabackend				\
	-DG_LOG_DOMAIN=\"libgoaidentity\"			\
	-DGOA_BACKEND_COMPILATION				\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DGOA_API_IS_SUBJECT_TO_CHANGE				\
	-DGOA_BACKEND_API_IS_SUBJECT_TO_CHANGE			\
	$(WARN_CFLAGS)						\
	$(NULL)

identity_headers =						\
	goaalarm.h						\
	goaidentity.h						\
	goaidentityinquiry.h					\
	goaidentityinquiryprivate.h				\
	goaidentityservice.h					\
	goaidentitymanagerprivate.h				\
	goaidentitymanager.h					\
	goaidentityutils.h					\
	goakerberosidentity.h					\
	goakerberosidentityinquiry.h				\
	goakerberosidentitymanager.h				\
	$(NULL)

identity_sources =						\
	$(identity_headers)					\
	goaalarm.c						\
	goaidentity.c						\
	goaidentityinquiry.c					\
	goaidentityservice.c					\
	goaidentitymanager.c					\
	goaidentityutils.c					\
	goakerberosidentity.c					\
	goakerberosidentityinquiry.c				\
	goakerberosidentitymanager.c				\
	main.c							\
	$(NULL)

identity_dbus_built_sources =					\
	org.gnome.Identity.c		org.gnome.Identity.h	\
	$(NULL)

$(identity_dbus_built_sources) : Makefile.am org.gnome.Identity.xml
	$(AM_V_GEN) gdbus-codegen						\
		--interface-prefix org.gnome.Identity.		\
		--c-namespace GoaIdentityService		\
		--c-generate-object-manager			\
		--generate-c-code org.gnome.Identity		\
		$(srcdir)/org.gnome.Identity.xml		\
		--annotate "org.gnome.Identity"			\
		           "org.gtk.GDBus.C.Name" Identity	\
		$(NULL)
BUILT_SOURCES += $(identity_dbus_built_sources)
EXTRA_DIST += org.gnome.Identity.xml

goaidentityenumtypes.h: goaidentityenumtypes.h.in $(identity_headers)
	$(AM_V_GEN) glib-mkenums --template $^ > $@
EXTRA_DIST += goaidentityenumtypes.h.in
BUILT_SOURCES += goaidentityenumtypes.h

goaidentityenumtypes.c: goaidentityenumtypes.c.in $(identity_headers)
	$(AM_V_GEN) glib-mkenums --template $^ > $@
EXTRA_DIST += goaidentityenumtypes.c.in
BUILT_SOURCES += goaidentityenumtypes.c

realmd_dbus_built_sources =					\
	org.freedesktop.realmd.h				\
	org.freedesktop.realmd.c				\
	$(NULL)

$(realmd_dbus_built_sources) : Makefile.am org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen						\
		--interface-prefix org.freedesktop.realmd.	\
		--generate-c-code org.freedesktop.realmd	\
		--c-generate-object-manager			\
		--c-namespace GoaRealm				\
		--annotate "org.freedesktop.realmd.Realm"	\
		            org.gtk.GDBus.C.Name Common		\
		$(srcdir)/org.freedesktop.realmd.xml		\
		$(NULL)
BUILT_SOURCES += $(realmd_dbus_built_sources)
EXTRA_DIST += org.freedesktop.realmd.xml

if BUILD_KERBEROS
libexec_PROGRAMS = goa-identity-service

goa_identity_service_SOURCES = 					\
	goaidentityenumtypes.h		goaidentityenumtypes.c	\
	$(identity_dbus_built_sources)				\
	$(realmd_dbus_built_sources)				\
	$(identity_sources)					\
	$(NULL)

goa_identity_service_CFLAGS =					\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS)						\
	$(KRB5_CFLAGS)						\
	$(GCR_CFLAGS)						\
	$(NULL)

goa_identity_service_LDADD = 					\
	$(top_builddir)/src/goa/libgoa-1.0.la			\
	$(GLIB_LIBS) 						\
	$(GTK_LIBS)						\
	$(KRB5_LIBS)						\
	$(GCR_LIBS)						\
	$(NULL)

else
EXTRA_DIST += $(identity_sources)
endif

CLEANFILES += $(BUILT_SOURCES)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
