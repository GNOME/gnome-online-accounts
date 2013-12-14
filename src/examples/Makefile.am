
NULL =

INCLUDES = 							\
	-I$(top_builddir)/src -I$(top_srcdir)/src		\
	-I$(top_builddir)/src/goa				\
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

noinst_PROGRAMS = list-providers list-accounts tweet add-pocket

list_providers_SOURCES = list-providers.c
list_providers_CFLAGS = $(GLIB_CFLAGS) $(GTK_CFLAGS)
list_providers_LDADD = $(GLIB_LIBS) $(GTK_LIBS) ../goabackend/libgoa-backend-1.0.la ../goa/libgoa-1.0.la

list_accounts_SOURCES = list-accounts.c
list_accounts_CFLAGS = $(GLIB_CFLAGS)
list_accounts_LDADD = $(GLIB_LIBS) ../goa/libgoa-1.0.la

tweet_SOURCES = tweet.c
tweet_CFLAGS = $(GLIB_CFLAGS) $(REST_CFLAGS)
tweet_LDADD = $(GLIB_LIBS) $(REST_LIBS) ../goa/libgoa-1.0.la

add_pocket_SOURCES = add-pocket.c
add_pocket_CFLAGS = $(GLIB_CFLAGS) $(REST_CFLAGS)
add_pocket_LDADD = $(GLIB_LIBS) $(REST_LIBS) ../goa/libgoa-1.0.la

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
