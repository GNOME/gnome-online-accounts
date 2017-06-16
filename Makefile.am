
NULL =

SUBDIRS = data

if BUILD_BACKEND
if BUILD_TELEPATHY
SUBDIRS += telepathy-account-widgets
endif
endif

SUBDIRS += src

if BUILD_BACKEND
SUBDIRS += doc
endif

SUBDIRS += po

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCHECK_CONFIGURE_FLAGS=--enable-introspection --enable-gtk-doc --enable-telepathy

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/gtk-doc.make \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(NULL)

GITIGNOREFILES = \
	m4 \
	$(NULL)

-include $(top_srcdir)/git.mk
