AC_INIT([GNOME Online Accounts],
        [3.6.2],
        [https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-online-accounts],
        [gnome-online-accounts])

AM_INIT_AUTOMAKE([1.9 foreign dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE([enable])

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

AC_ARG_ENABLE([documentation],
	AS_HELP_STRING([--enable-documentation], [enable man pages and HTML]),
	[], [enable_documentation=yes])
AM_CONDITIONAL(ENABLE_DOCUMENTATION, test x$enable_documentation = xyes)
if test x$enable_documentation = xyes; then
   AC_PATH_PROG([XSLTPROC], [xsltproc])
   if test -z "$XSLTPROC"; then
     AC_MSG_ERROR([xsltproc is needed])
   fi
fi

GTK_DOC_CHECK([1.3])

# Libraries
#

PKG_CHECK_MODULES(GLIB, [glib-2.0 gio-2.0 gio-unix-2.0 >= 2.32])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.5.1])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(WEBKIT_GTK, [webkitgtk-3.0])
AC_SUBST(WEBKIT_GTK_CFLAGS)
AC_SUBST(WEBKIT_GTK_LIBS)

PKG_CHECK_MODULES(LIBSOUP, [libsoup-gnome-2.4 >= 2.38])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
AC_SUBST(JSON_GLIB_CFLAGS)
AC_SUBST(JSON_GLIB_LIBS)

PKG_CHECK_MODULES(LIBNOTIFY, [libnotify >= 0.7])
AC_SUBST(LIBNOTIFY_CFLAGS)
AC_SUBST(LIBNOTIFY_LIBS)

PKG_CHECK_MODULES(REST, [rest-0.7])
AC_SUBST(REST_CFLAGS)
AC_SUBST(REST_LIBS)

PKG_CHECK_MODULES(SECRET, [libsecret-1])
AC_SUBST(SECRET_CFLAGS)
AC_SUBST(SECRET_LIBS)

PKG_CHECK_MODULES(LIBXML, [libxml-2.0])
AC_SUBST(LIBXML_CFLAGS)
AC_SUBST(LIBXML_LIBS)

GOBJECT_INTROSPECTION_CHECK([0.6.2])

# service providers
#

# Microsoft Exchange
AC_ARG_ENABLE([exchange],
              [AS_HELP_STRING([--enable-exchange],
              [Enable Microsoft Exchange provider])],
              [],
              [enable_exchange=yes])
if test "$enable_exchange" != "no"; then
  AC_DEFINE(GOA_EXCHANGE_ENABLED, 1, [Enable Microsoft Exchange data provider])
fi

# Google
AC_ARG_ENABLE([google], [AS_HELP_STRING([--enable-google], [Enable Google provider])], [], [enable_google=yes])
AC_ARG_WITH(google-consumer-key,
            [AS_HELP_STRING([--with-google-consumer-key],
                            [Google OAuth 1.0 consumer key])],
                            [],
                            [])
AC_ARG_WITH(google-consumer-secret,
            [AS_HELP_STRING([--with-google-consumer-secret],
                            [Google OAuth 1.0 consumer secret])],
                            [],
                            [])
if test "$with_google_consumer_key" = ""; then
  with_google_consumer_key=anonymous
fi
if test "$with_google_consumer_secret" = ""; then
  with_google_consumer_secret=anonymous
fi
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CONSUMER_KEY, ["$with_google_consumer_key"], [Google OAuth 1.0 consumer key])
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CONSUMER_SECRET, ["$with_google_consumer_secret"], [Google OAuth 1.0 consumer secret])
if test "$enable_google" != "no"; then
  AC_DEFINE(GOA_GOOGLE_ENABLED, 1, [Enable Google data provider])
fi

# Facebook
AC_ARG_ENABLE([facebook], [AS_HELP_STRING([--enable-facebook],
                                          [Enable Facebook provider])],
                                          [],
                                          [enable_facebook=yes])
AC_ARG_WITH(facebook-client-id,
            [AS_HELP_STRING([--with-facebook-client-id],
                            [Facebook OAuth 2.0 client id])],
                            [],
                            [])

if test "$with_facebook_client_id" = ""; then
  with_facebook_client_id=297654143624603
fi
AC_DEFINE_UNQUOTED(GOA_FACEBOOK_CLIENT_ID,
                   ["$with_facebook_client_id"],
                   [Facebook OAuth 2.0 client id])
if test "$enable_facebook" != "no"; then
  AC_DEFINE(GOA_FACEBOOK_ENABLED, 1, [Enable Facebook data provider])
fi

# Windows Live
AC_ARG_ENABLE([windows-live], [AS_HELP_STRING([--enable-windows-live],
                                              [Enable Windows Live provider])],
                                              [],
                                              [enable_windows_live=yes])
AC_ARG_WITH(windows-live-client-id,
            [AS_HELP_STRING([--with-windows-live-client-id],
                            [Windows Live OAuth 2.0 client id])],
                            [],
                            [])
if test "$with_windows_live_client_id" = ""; then
  with_windows_live_client_id=0000000044067703
fi
AC_DEFINE_UNQUOTED(GOA_WINDOWS_LIVE_CLIENT_ID,
                   ["$with_windows_live_client_id"],
                   [Windows Live OAuth 2.0 client id])
if test "$enable_windows_live" != "no"; then
  AC_DEFINE(GOA_WINDOWS_LIVE_ENABLED, 1, [Enable Windows Live data provider])
fi

# Kerberos
AC_ARG_ENABLE([kerberos],
              [AS_HELP_STRING([--enable-kerberos],
              [Enable kerberos support])],
              [with_kerberos=$enableval],
              [enable_kerberos=no])
AC_PATH_PROG([KRB5_CONFIG], krb5-config, none, $PATH:/usr/kerberos/bin)

if test "x$KRB5_CONFIG" != "xnone"; then
    KRB5_LIBS="`${KRB5_CONFIG} --libs krb5`"
    KRB5_CFLAGS="`${KRB5_CONFIG} --cflags krb5`"
    have_kerberos=yes
else
    KRB5_LIBS=""
    KRB5_CFLAGS=""
    have_kerberos=no
fi
AC_SUBST(KRB5_CFLAGS)
AC_SUBST(KRB5_LIBS)

if test "$enable_kerberos" != "no"; then
  if test "$have_kerberos" = "no" ; then
      AC_MSG_ERROR([kerberos support requested, but not available])
  fi

  PKG_CHECK_MODULES(GCR, gcr-3)
  AC_DEFINE(GCR_API_SUBJECT_TO_CHANGE, 1, [Define to use the GCR API])
  AC_DEFINE(GOA_KERBEROS_ENABLED, 1, [Enable Enterprise Identity data provider])
fi
AM_CONDITIONAL(BUILD_KERBEROS, [test x$enable_kerberos != xno])

# Optional timerfd support
AC_MSG_CHECKING([for timerfd support])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#include <sys/timerfd.h>
#include <unistd.h>
],[
int
main (void)
{
  struct itimerspec timer_spec = { 0 };
  timerfd_settime (timerfd_create (CLOCK_MONOTONIC, TFD_CLOEXEC),
                   TFD_TIMER_ABSTIME,
                   &timer_spec,
                   NULL);

  return 0;
}
])],
[have_timerfd=yes],
[have_timerfd=no])
AC_MSG_RESULT($have_timerfd)
if test x"$have_timerfd" = x"yes"; then
    AC_DEFINE(HAVE_TIMERFD, 1, [have timerfd support])

    dnl libc headers tend to trail kernel support
    dnl so compensate if necessary
    AC_MSG_CHECKING([for timerfd cancel-on-set support])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
    #include <sys/timerfd.h>
    #include <unistd.h>
    ],[
    int
    main (void)
    {
      struct itimerspec timer_spec = { 0 };
      timerfd_settime (timerfd_create (CLOCK_MONOTONIC, TFD_CLOEXEC),
                       TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
                       &timer_spec,
                       NULL);

      return 0;
    }
    ])],
    [have_tfd_timer_cancel_on_set=yes],
    [have_tfd_timer_cancel_on_set=no])
    AC_MSG_RESULT($have_tfd_timer_cancel_on_set)
    if test x"$have_tfd_timer_cancel_on_set" = x"no"; then
       AC_DEFINE(TFD_TIMER_CANCEL_ON_SET, [(1 << 1)], [have timerfd support])
    fi
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
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/256x256/Makefile
src/Makefile
src/goa/Makefile
src/goa/goa-1.0.pc
src/goabackend/Makefile
src/goabackend/goa-backend-1.0.pc
src/goaidentity/Makefile
src/daemon/Makefile
src/examples/Makefile
po/Makefile.in
doc/Makefile
doc/version.xml
])

dnl ==========================================================================
echo "
               gnome-online-accounts $VERSION
             ===============================

	prefix:                         ${prefix}
	libdir:                         ${libdir}
	libexecdir:                     ${libexecdir}
	bindir:                         ${bindir}
	sbindir:                        ${sbindir}
	datadir:                        ${datadir}
	sysconfdir:                     ${sysconfdir}
	localstatedir:                  ${localstatedir}

	compiler:                       ${CC}
	cflags:                         ${CFLAGS}
	cppflags:                       ${CPPFLAGS}
	introspection:		        ${found_introspection}

	Google provider:                ${enable_google} (OAuth 1.0, key:${with_google_consumer_key} secret:${with_google_consumer_secret})
	Microsoft Exchange provider:    ${enable_exchange}
	Kerberos provider:              ${enable_kerberos}
	Facebook provider:              ${enable_facebook} (OAuth 2.0, id:${with_facebook_client_id})
	Windows Live provider:          ${enable_windows_live} (OAuth 2.0, id:${with_windows_live_client_id})

	Maintainer mode:                ${USE_MAINTAINER_MODE}
	Building api docs:              ${enable_gtk_doc}
"
