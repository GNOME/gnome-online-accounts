
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
	$(WARN_CFLAGS)						\
	$(NULL)

noinst_PROGRAMS = mail-query

mail_query_SOURCES = 						\
				mail-query.c			\
	$(NULL)

mail_query_CFLAGS =						\
	$(GLIB_CFLAGS) 						\
	$(NULL)

mail_query_LDADD = 						\
	$(GLIB_LIBS) 						\
	$(top_builddir)/src/goa/libgoa.la			\
	$(NULL)

clean-local :
	rm -f *~
