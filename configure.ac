m4_define([goa_major_version], [3])
m4_define([goa_minor_version], [11])
m4_define([goa_micro_version], [3])
m4_define([goa_nano_version], [0])

m4_define([goa_base_version],
          [goa_major_version.goa_minor_version.goa_micro_version])
m4_define([goa_version],
          [m4_if(goa_nano_version, 0, [goa_base_version],
                 [goa_base_version].[goa_nano_version])])

AC_INIT([GNOME Online Accounts],
        [goa_version],
        [https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-online-accounts],
        [gnome-online-accounts])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.9 foreign dist-xz no-dist-gzip tar-ustar])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE([enable])

AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

GOA_MAJOR_VERSION=goa_major_version
GOA_MINOR_VERSION=goa_minor_version
GOA_MICRO_VERSION=goa_micro_version

AC_SUBST(GOA_MAJOR_VERSION)
AC_SUBST(GOA_MINOR_VERSION)
AC_SUBST(GOA_MICRO_VERSION)

# Before making a release, the LT_VERSION string should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
GOA_LT_VERSION=0:0:0
GOA_BACKEND_LT_VERSION=1:0:0
AC_SUBST(GOA_LT_VERSION)
AC_SUBST(GOA_BACKEND_LT_VERSION)

AC_DEFINE(GOA_MAJOR_VERSION,
          [goa_major_version],
          [Define to the GOA major version])
AC_DEFINE(GOA_MINOR_VERSION,
          [goa_minor_version],
          [Define to the GOA minor version])
AC_DEFINE(GOA_MICRO_VERSION,
          [goa_micro_version],
          [Define to the GOA micro version])

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

PKG_CHECK_MODULES(GLIB, [glib-2.0 gio-2.0 gio-unix-2.0 >= 2.35])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.5.1])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(WEBKIT_GTK, [webkitgtk-3.0 >= 2.1.90])
AC_SUBST(WEBKIT_GTK_CFLAGS)
AC_SUBST(WEBKIT_GTK_LIBS)

PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4 >= 2.41])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
AC_SUBST(JSON_GLIB_CFLAGS)
AC_SUBST(JSON_GLIB_LIBS)

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

# telepathy-account-widgets
prev_top_build_prefix=$ac_top_build_prefix
prev_ac_configure_args=$ac_configure_args
ac_configure_args="$ac_configure_args --with-cheese=no --disable-gudev"
# install the data together with the rest of GOA's data
ac_configure_args="$ac_configure_args --with-pkgdatadir=${datadir}/gnome-online-accounts"
# GOA ships its icons directly in ${datadir}/icons (usually /usr/share/icons/) and not
# in its own data directory
ac_configure_args="$ac_configure_args --with-icondir=${datadir}/icons"
# Change the name of tpaw's .mo files to avoid conflicts with other packages using tp-aw
ac_configure_args="$ac_configure_args --with-gettext-package=gnome-online-accounts-tpaw"
# Disable the installation of the GSettings schema to avoid multiple components shipping
# the same file. See https://bugzilla.gnome.org/show_bug.cgi?id=706803
ac_configure_args="$ac_configure_args --disable-schemas-compile --disable-settings"
AX_CONFIG_DIR([telepathy-account-widgets])
ac_top_build_prefix=$prev_top_build_prefix
ac_configure_args=$prev_ac_configure_args

export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$ac_top_build_prefix"telepathy-account-widgets/
PKG_CHECK_MODULES(TPAW, telepathy-account-widgets)

AC_ARG_ENABLE([inspector],
              [AS_HELP_STRING([--enable-inspector],
              [Enable a WebKitWebInspector for the embedded web view])],
              [],
              [enable_inspector=no])
if test "$enable_inspector" != "no"; then
  AC_DEFINE(GOA_INSPECTOR_ENABLED, 1, [Enable a WebKitWebInspector for the embedded web view])
fi

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

# Flickr
AC_ARG_ENABLE([flickr], [AS_HELP_STRING([--enable-flickr], [Enable Flickr provider])], [], [enable_flickr=yes])
AC_ARG_WITH(flickr-consumer-key,
            [AS_HELP_STRING([--with-flickr-consumer-key],
                            [Flickr OAuth 1.0 consumer key])],
                            [],
                            [])
AC_ARG_WITH(flickr-consumer-secret,
            [AS_HELP_STRING([--with-flickr-consumer-secret],
                            [Flickr OAuth 1.0 consumer secret])],
                            [],
                            [])
if test "$with_flickr_consumer_key" = ""; then
  with_flickr_consumer_key=ed00ad7e0869897506e23c0d18e34d01
fi
if test "$with_flickr_consumer_secret" = ""; then
  with_flickr_consumer_secret=ebd556dd187188b1
fi
AC_DEFINE_UNQUOTED(GOA_FLICKR_CONSUMER_KEY, ["$with_flickr_consumer_key"], [Flickr OAuth 1.0 consumer key])
AC_DEFINE_UNQUOTED(GOA_FLICKR_CONSUMER_SECRET, ["$with_flickr_consumer_secret"], [Flickr OAuth 1.0 consumer secret])
if test "$enable_flickr" != "no"; then
  AC_DEFINE(GOA_FLICKR_ENABLED, 1, [Enable Flickr data provider])
fi

# Google
AC_ARG_ENABLE([google], [AS_HELP_STRING([--enable-google], [Enable Google provider])], [], [enable_google=yes])
AC_ARG_WITH(google-client-id,
            [AS_HELP_STRING([--with-google-client-id],
                            [Google OAuth 2.0 client id])],
                            [],
                            [])
AC_ARG_WITH(google-client-secret,
            [AS_HELP_STRING([--with-google-client-secret],
                            [Google OAuth 2.0 client secret])],
                            [],
                            [])
if test "$with_google_client_id" = ""; then
  with_google_client_id=923794261470.apps.googleusercontent.com
fi
if test "$with_google_client_secret" = ""; then
  with_google_client_secret=SlVBAcxamM0TBPlvX2c1zbEY
fi
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CLIENT_ID,
                   ["$with_google_client_id"],
                   [Google OAuth 2.0 client id])
AC_DEFINE_UNQUOTED(GOA_GOOGLE_CLIENT_SECRET,
                   ["$with_google_client_secret"],
                   [Google OAuth 2.0 client secret])
if test "$enable_google" != "no"; then
  AC_DEFINE(GOA_GOOGLE_ENABLED, 1, [Enable Google data provider])
fi

# IMAP/SMTP
AC_ARG_ENABLE([imap-smtp],
              [AS_HELP_STRING([--enable-imap-smtp],
              [Enable IMAP/SMTP provider])],
              [],
              [enable_imap_smtp=yes])
if test "$enable_imap_smtp" != "no"; then
  AC_DEFINE(GOA_IMAP_SMTP_ENABLED, 1, [Enable IMAP/SMTP data provider])
fi

# ownCloud
AC_ARG_ENABLE([owncloud],
              [AS_HELP_STRING([--enable-owncloud],
              [Enable ownCloud provider])],
              [],
              [enable_owncloud=yes])
if test "$enable_owncloud" != "no"; then
  AC_DEFINE(GOA_OWNCLOUD_ENABLED, 1, [Enable ownCloud data provider])
fi

# Twitter
AC_ARG_ENABLE([twitter], [AS_HELP_STRING([--enable-twitter], [Enable Twitter provider])], [], [enable_twitter=no])
AC_ARG_WITH(twitter-consumer-key,
            [AS_HELP_STRING([--with-twitter-consumer-key],
                            [Twitter OAuth 1.0 consumer key])],
                            [],
                            [])
AC_ARG_WITH(twitter-consumer-secret,
            [AS_HELP_STRING([--with-twitter-consumer-secret],
                            [Twitter OAuth 1.0 consumer secret])],
                            [],
                            [])
if test "$with_twitter_consumer_key" = ""; then
  with_twitter_consumer_key=not_set
fi
if test "$with_twitter_consumer_secret" = ""; then
  with_twitter_consumer_secret=not_set
fi
AC_DEFINE_UNQUOTED(GOA_TWITTER_CONSUMER_KEY, ["$with_twitter_consumer_key"], [Twitter OAuth 1.0 consumer key])
AC_DEFINE_UNQUOTED(GOA_TWITTER_CONSUMER_SECRET, ["$with_twitter_consumer_secret"], [Twitter OAuth 1.0 consumer secret])
if test "$enable_twitter" != "no"; then
  AC_DEFINE(GOA_TWITTER_ENABLED, 1, [Enable Twitter data provider])
fi

# Yahoo
AC_ARG_ENABLE([yahoo], [AS_HELP_STRING([--enable-yahoo], [Enable Yahoo provider])], [], [enable_yahoo=no])
AC_ARG_WITH(yahoo-consumer-key,
            [AS_HELP_STRING([--with-yahoo-consumer-key],
                            [Yahoo OAuth 1.0 consumer key])],
                            [],
                            [])
AC_ARG_WITH(yahoo-consumer-secret,
            [AS_HELP_STRING([--with-yahoo-consumer-secret],
                            [Yahoo OAuth 1.0 consumer secret])],
                            [],
                            [])
if test "$with_yahoo_consumer_key" = ""; then
  with_yahoo_consumer_key=not_set
fi
if test "$with_yahoo_consumer_secret" = ""; then
  with_yahoo_consumer_secret=not_set
fi
AC_DEFINE_UNQUOTED(GOA_YAHOO_CONSUMER_KEY, ["$with_yahoo_consumer_key"], [Yahoo OAuth 1.0 consumer key])
AC_DEFINE_UNQUOTED(GOA_YAHOO_CONSUMER_SECRET, ["$with_yahoo_consumer_secret"], [Yahoo OAuth 1.0 consumer secret])
if test "$enable_yahoo" != "no"; then
  AC_DEFINE(GOA_YAHOO_ENABLED, 1, [Enable Yahoo data provider])
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

# Telepathy
AC_ARG_ENABLE([telepathy],
              [AS_HELP_STRING([--enable-telepathy],
              [Enable Telepathy IM provider])],
              [],
              [enable_telepathy=yes])
if test "$enable_telepathy" != "no"; then
  AC_DEFINE(GOA_TELEPATHY_ENABLED, 1, [Enable Telepathy data provider])
fi

PKG_CHECK_MODULES(TP, telepathy-glib)

# Pocket
AC_ARG_ENABLE([pocket], [AS_HELP_STRING([--enable-pocket],
                                              [Enable Pocket provider])],
                                              [],
                                              [enable_pocket=no])
AC_ARG_WITH(pocket-client-id,
            [AS_HELP_STRING([--with-pocket-client-id],
                            [Pocket OAuth 2.0 client id])],
                            [],
                            [])
if test "$with_pocket_client_id" = ""; then
  with_pocket_client_id="16630-40b25246b56e8ad5310b2883"
fi
AC_DEFINE_UNQUOTED(GOA_POCKET_CLIENT_ID,
                   ["$with_pocket_client_id"],
                   [Pocket OAuth 2.0 client id])
if test "$enable_pocket" != "no"; then
  AC_DEFINE(GOA_POCKET_ENABLED, 1, [Enable Pocket data provider])
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

# goaconfig.h
#

AC_CONFIG_COMMANDS([src/goa/goaconfig.h],
[
	outfile=src/goa/goaconfig.h-tmp
	cat > $outfile <<\_______EOF
/* goaconfig.h
 *
 * This is a generated file.  Please modify 'configure.ac'
 */

#ifndef __GOA_CONFIG_H__
#define __GOA_CONFIG_H__

G_BEGIN_DECLS

_______EOF
	echo "#define GOA_MAJOR_VERSION $GOA_MAJOR_VERSION" >> $outfile
	echo "#define GOA_MINOR_VERSION $GOA_MINOR_VERSION" >> $outfile
	echo "#define GOA_MICRO_VERSION $GOA_MICRO_VERSION" >> $outfile
	cat >> $outfile <<\_______EOF

G_END_DECLS

#endif /* __GOA_CONFIG_H__ */
_______EOF


	if cmp -s $outfile src/goa/goaconfig.h; then
	  AC_MSG_NOTICE([src/goa/goaconfig.h is unchanged])
	  rm -f $outfile
	else
	  mv $outfile src/goa/goaconfig.h
	fi
],[
	GOA_MAJOR_VERSION=$GOA_MAJOR_VERSION
	GOA_MINOR_VERSION=$GOA_MINOR_VERSION
	GOA_MICRO_VERSION=$GOA_MICRO_VERSION
])

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
	inspector:		        ${enable_inspector}
	introspection:		        ${found_introspection}

	Flickr provider:                ${enable_flickr} (OAuth 1.0, key:${with_flickr_consumer_key} secret:${with_flickr_consumer_secret})
	Google provider:                ${enable_google} (OAuth 2.0, id:${with_google_client_id} secret:${with_google_client_secret})
	IMAP/SMTP provider:             ${enable_imap_smtp}
	Microsoft Exchange provider:    ${enable_exchange}
	ownCloud provider:              ${enable_owncloud}
	Kerberos provider:              ${enable_kerberos}
	Twitter provider:               ${enable_twitter} (OAuth 1.0, key:${with_twitter_consumer_key} secret:${with_twitter_consumer_secret})
	Yahoo provider:                 ${enable_yahoo} (OAuth 1.0, key:${with_yahoo_consumer_key} secret:${with_yahoo_consumer_secret})
	Facebook provider:              ${enable_facebook} (OAuth 2.0, id:${with_facebook_client_id})
	Windows Live provider:          ${enable_windows_live} (OAuth 2.0, id:${with_windows_live_client_id})
        Telepathy provider:             ${enable_telepathy}
        Pocket provider:                ${enable_pocket} (id:${with_pocket_client_id})

	Maintainer mode:                ${USE_MAINTAINER_MODE}
	Building api docs:              ${enable_gtk_doc}
"
