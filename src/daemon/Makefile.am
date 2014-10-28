NULL =

AM_CPPFLAGS = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
	-I$(top_builddir)/src/goaidentity			\
	-I$(top_srcdir)/src/goaidentity				\
	-DG_LOG_DOMAIN=\"goa-daemon\"				\
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

BUILT_SOURCES = $(NULL)
EXTRA_DIST = $(NULL)

libexec_PROGRAMS = goa-daemon

goa_daemon_SOURCES = 						\
				main.c				\
	goadaemontypes.h					\
	goadaemon.h		goadaemon.c			\
	goatpaccountlinker.h	goatpaccountlinker.c		\
	$(NULL)

goa_daemon_CFLAGS =						\
	$(GLIB_CFLAGS) 						\
	$(GTK_CFLAGS)						\
	$(REST_CFLAGS)						\
	$(TP_CFLAGS)						\
	$(NULL)

goa_daemon_LDADD = 						\
	$(GLIB_LIBS) 						\
	$(top_builddir)/src/goa/libgoa-1.0.la			\
	$(top_builddir)/src/goabackend/libgoa-backend-1.0.la	\
	$(GTK_LIBS)						\
	$(REST_LIBS)						\
	$(TP_LIBS)						\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
