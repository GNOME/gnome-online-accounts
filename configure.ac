AC_INIT([goa],[0.1],[https://bugzilla.gnome.org/enter_bug.cgi?product=goa],[goa])

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

# Internationalization
#

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])
GETTEXT_PACKAGE=goa
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# Generate
#

AC_OUTPUT([
Makefile
data/Makefile
src/Makefile
src/goa/Makefile
src/daemon/Makefile
src/panel/Makefile
po/Makefile.in
doc/Makefile
doc/version.xml
])

dnl ==========================================================================
echo "
               goa $VERSION
             =============

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

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Building api docs:          ${enable_gtk_doc}
"
