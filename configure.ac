AC_INIT([gnome-online-accounts],[3.1.0],[https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-online-accounts],[gnome-online-accounts])

AM_INIT_AUTOMAKE([1.9 foreign dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE

AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Initialization
#

GNOME_COMMON_INIT
GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc is needed])
fi

GTK_DOC_CHECK([1.3])

# Libraries
#

PKG_CHECK_MODULES(GLIB, [glib-2.0 gio-2.0 gio-unix-2.0 >= 2.28.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.0.0])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GNOME_CONTROL_CENTER, [libgnome-control-center >= 3.0.0])
AC_SUBST(GNOME_CONTROL_CENTER_CFLAGS)
AC_SUBST(GNOME_CONTROL_CENTER_LIBS)

PKG_CHECK_MODULES(WEBKIT_GTK, [webkitgtk-3.0])
AC_SUBST(WEBKIT_GTK_CFLAGS)
AC_SUBST(WEBKIT_GTK_LIBS)

PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
AC_SUBST(JSON_GLIB_CFLAGS)
AC_SUBST(JSON_GLIB_LIBS)

PKG_CHECK_MODULES(GNOME_KEYRING, [gnome-keyring-1])
AC_SUBST(GNOME_KEYRING_CFLAGS)
AC_SUBST(GNOME_KEYRING_LIBS)

PKG_CHECK_MODULES(LIBNOTIFY, [libnotify >= 0.7])
AC_SUBST(LIBNOTIFY_CFLAGS)
AC_SUBST(LIBNOTIFY_LIBS)

PKG_CHECK_MODULES(REST, [rest-0.7])
AC_SUBST(REST_CFLAGS)
AC_SUBST(REST_LIBS)

GOBJECT_INTROSPECTION_CHECK([0.6.2])

# service providers
#

# Google
with_google_consumer_key=anonymous
AC_ARG_WITH([google-consumer-key=KEY],
            [AS_HELP_STRING([--with-google-consumer-key],
                            [Google OAuth 1.0 consumer key (anonymous)])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CONSUMER_KEY, ["$with_google_consumer_key"], [Google OAuth 1.0 consumer key])
with_google_consumer_secret=anonymous
AC_ARG_WITH([google-consumer-secret=SECRET],
            [AS_HELP_STRING([--with-google-consumer-secret],
                            [Google OAuth 1.0 consumer secret (anonymous)])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CONSUMER_SECRET, ["$with_google_consumer_secret"], [Google OAuth 1.0 consumer secret])
AC_ARG_ENABLE([google], [AS_HELP_STRING([--enable-google], [Enable Google provider])], [], [enable_google=yes])
if test "$enable_google" != "no"; then
  AC_DEFINE(GOA_GOOGLE_ENABLED, 1, [Enable Google data provider])
fi

# Twitter
with_twitter_consumer_key=not_set
AC_ARG_WITH([twitter-consumer-key=KEY],
            [AS_HELP_STRING([--with-twitter-consumer-key],
                            [Twitter OAuth 1.0 consumer key])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_TWITTER_CONSUMER_KEY, ["$with_twitter_consumer_key"], [Twitter OAuth 1.0 consumer key])
with_twitter_consumer_secret=not_set
AC_ARG_WITH([twitter-consumer-secret=SECRET],
            [AS_HELP_STRING([--with-twitter-consumer-secret],
                            [Twitter OAuth 1.0 consumer secret])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_TWITTER_CONSUMER_SECRET, ["$with_twitter_consumer_secret"], [Twitter OAuth 1.0 consumer secret])
AC_ARG_ENABLE([twitter], [AS_HELP_STRING([--enable-twitter], [Enable Twitter provider])], [], [enable_twitter=no])
if test "$enable_twitter" != "no"; then
  AC_DEFINE(GOA_TWITTER_ENABLED, 1, [Enable Twitter data provider])
fi

# Yahoo
with_yahoo_consumer_key=not_set
AC_ARG_WITH([yahoo-consumer-key=KEY],
            [AS_HELP_STRING([--with-yahoo-consumer-key],
                            [Yahoo OAuth 1.0 consumer key])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_YAHOO_CONSUMER_KEY, ["$with_yahoo_consumer_key"], [Yahoo OAuth 1.0 consumer key])
with_yahoo_consumer_secret=not_set
AC_ARG_WITH([yahoo-consumer-secret=SECRET],
            [AS_HELP_STRING([--with-yahoo-consumer-secret],
                            [Yahoo OAuth 1.0 consumer secret])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_YAHOO_CONSUMER_SECRET, ["$with_yahoo_consumer_secret"], [Yahoo OAuth 1.0 consumer secret])
AC_ARG_ENABLE([yahoo], [AS_HELP_STRING([--enable-yahoo], [Enable Yahoo provider])], [], [enable_yahoo=no])
if test "$enable_yahoo" != "no"; then
  AC_DEFINE(GOA_YAHOO_ENABLED, 1, [Enable Yahoo data provider])
fi

# Facebook
with_facebook_client_id=not_set
AC_ARG_WITH([facebook-client-id=ID],
            [AS_HELP_STRING([--with-facebook-client-id],
                            [Facebook OAuth 2.0 client id])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_FACEBOOK_CLIENT_ID, ["$with_facebook_client_id"], [Facebook OAuth 2.0 client id])
with_facebook_client_secret=not_set
AC_ARG_WITH([facebook-client-secret=SECRET],
            [AS_HELP_STRING([--with-facebook-client-secret],
                            [Facebook OAuth 2.0 client secret])],
                            [],
                            [])
AC_DEFINE_UNQUOTED(GOA_FACEBOOK_CLIENT_SECRET, ["$with_facebook_client_secret"], [Facebook OAuth 2.0 client secret])
AC_ARG_ENABLE([facebook], [AS_HELP_STRING([--enable-facebook], [Enable Facebook provider])], [], [enable_facebook=no])
if test "$enable_facebook" != "no"; then
  AC_DEFINE(GOA_FACEBOOK_ENABLED, 1, [Enable Facebook data provider])
fi


# Internationalization
#

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])
GETTEXT_PACKAGE=gnome-online-accounts
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# Generate
#

AC_OUTPUT([
Makefile
data/Makefile
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/256x256/Makefile
data/icons/scalable/Makefile
src/Makefile
src/goa/Makefile
src/goa/goa-1.0.pc
src/goabackend/Makefile
src/goabackend/goa-backend-1.0.pc
src/daemon/Makefile
src/panel/Makefile
src/examples/Makefile
po/Makefile.in
doc/Makefile
doc/version.xml
])

dnl ==========================================================================
echo "
               gnome-online-accounts $VERSION
             ===============================

        prefix:                     ${prefix}
        libdir:                     ${libdir}
        libexecdir:                 ${libexecdir}
        bindir:                     ${bindir}
        sbindir:                    ${sbindir}
        datadir:                    ${datadir}
        sysconfdir:                 ${sysconfdir}
        localstatedir:              ${localstatedir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}
	introspection:		    ${found_introspection}

	Google provider:            ${enable_google} (OAuth 1.0, key:${with_google_consumer_key} secret:${with_google_consumer_secret})
	Twitter provider:           ${enable_twitter} (OAuth 1.0, key:${with_twitter_consumer_key} secret:${with_twitter_consumer_secret})
	Yahoo provider:             ${enable_yahoo} (OAuth 1.0, key:${with_yahoo_consumer_key} secret:${with_yahoo_consumer_secret})
	Facebook provider:          ${enable_facebook} (OAuth 2.0, id:${with_facebook_client_id} secret:${with_facebook_client_secret})

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Building api docs:          ${enable_gtk_doc}
"
