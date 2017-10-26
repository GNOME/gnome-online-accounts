
NULL =

AM_CPPFLAGS = 							\
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

noinst_PROGRAMS = list-accounts lastfm-shout

list_accounts_SOURCES = list-accounts.c
list_accounts_CFLAGS = $(GLIB_CFLAGS)
list_accounts_LDADD = $(GLIB_LIBS) ../goa/libgoa-1.0.la
list_accounts_LDFLAGS = $(WARN_LDFLAGS)

lastfm_shout_SOURCES = lastfm-shout.c
lastfm_shout_CFLAGS = $(GLIB_CFLAGS) $(REST_CFLAGS)
lastfm_shout_LDADD = $(GLIB_LIBS) $(REST_LIBS) ../goa/libgoa-1.0.la
lastfm_shout_LDFLAGS = $(WARN_LDFLAGS)

if BUILD_BACKEND
noinst_PROGRAMS += introspect-providers list-providers add-pocket

introspect_providers_SOURCES = introspect-providers.c
introspect_providers_CFLAGS = $(GLIB_CFLAGS) $(GTK_CFLAGS)
introspect_providers_LDADD = $(GLIB_LIBS) $(GTK_LIBS) ../goabackend/libgoa-backend-1.0.la ../goa/libgoa-1.0.la
introspect_providers_LDFLAGS = $(WARN_LDFLAGS)

list_providers_SOURCES = list-providers.c
list_providers_CFLAGS = $(GLIB_CFLAGS) $(GTK_CFLAGS)
list_providers_LDADD = $(GLIB_LIBS) $(GTK_LIBS) ../goabackend/libgoa-backend-1.0.la ../goa/libgoa-1.0.la
list_providers_LDFLAGS = $(WARN_LDFLAGS)

add_pocket_SOURCES = add-pocket.c
add_pocket_CFLAGS = $(GLIB_CFLAGS) $(REST_CFLAGS)
add_pocket_LDADD = $(GLIB_LIBS) $(REST_LIBS) ../goa/libgoa-1.0.la
add_pocket_LDFLAGS = $(WARN_LDFLAGS)
endif

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
