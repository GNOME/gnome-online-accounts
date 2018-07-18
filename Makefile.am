
NULL =

SUBDIRS = data src

if BUILD_BACKEND
SUBDIRS += doc
endif

SUBDIRS += po

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-documentation \
	--enable-gtk-doc \
	--enable-introspection \
	$(NULL)

MAINTAINERCLEANFILES = \
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL) \
	$(srcdir)/INSTALL \
	$(srcdir)/config.h.in \
	$(srcdir)/gtk-doc.make \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(NULL)

GITIGNOREFILES = \
	m4 \
	$(NULL)

-include $(top_srcdir)/git.mk
