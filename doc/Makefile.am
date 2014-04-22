
NULL =

SUBDIRS =

# The name of the module.
DOC_MODULE=goa

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS=

# The directory containing the source code. Relative to $(srcdir)
DOC_SOURCE_DIR=$(top_srcdir)/src

HFILE_GLOB=$(top_srcdir)/src/goa/*.h $(top_srcdir)/src/goabackend/*.h
CFILE_GLOB=$(top_srcdir)/src/goa/*.c $(top_srcdir)/src/goabackend/*.c

# Headers to ignore
IGNORE_HFILES=				\
	config.h			\
	$(NULL)

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
AM_CPPFLAGS = \
	-I$(srcdir)			\
	-I$(top_srcdir)			\
	-I$(top_builddir)		\
	$(GLIB_CFLAGS)			\
	$(GIO_CFLAGS)			\
	$(NULL)

GTKDOC_LIBS = 							\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	$(top_builddir)/src/goa/libgoa-1.0.la			\
	$(top_builddir)/src/goabackend/libgoa-backend-1.0.la	\
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=goa

# Images to copy into HTML directory
HTML_IMAGES =			\
	$(NULL)

content_files =			\
	$(NULL)

expand_content_files =		\
	goa-daemon.xml		\
	goa-overview.xml	\
	$(NULL)

extra_files =			\
	$(NULL)

man_MANS =

if ENABLE_DOCUMENTATION
man_MANS +=			\
	goa-daemon.8		\
	$(NULL)
endif

.xml.8:
	$(AM_V_GEN) $(XSLTPROC) \
	  --nonet \
	  --stringparam man.output.quietly 1 \
	  --stringparam funcsynopsis.style ansi \
	  http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

if ENABLE_GTK_DOC
include $(top_srcdir)/gtk-doc.make
else
EXTRA_DIST =
CLEANFILES =
endif

EXTRA_DIST +=			\
	goa-daemon.xml		\
	goa-overview.xml	\
	version.xml.in		\
	$(NULL)

CLEANFILES +=   		\
	$(DOC_MODULE)-decl-list.txt	\
	$(DOC_MODULE)-decl.txt		\
	$(DOC_MODULE)-overrides.txt	\
	$(DOC_MODULE)-undeclared.txt	\
	$(DOC_MODULE)-undocumented.txt	\
	$(DOC_MODULE)-overrides.txt	\
	$(DOC_MODULE)-unused.txt	\
	$(DOC_MODULE).args		\
	$(DOC_MODULE).hierarchy		\
	$(DOC_MODULE).interfaces	\
	$(DOC_MODULE).prerequisites	\
	$(DOC_MODULE).signals		\
	*.stamp				\
	*.8				\
	-rf xml html tmpl		\
	$(NULL)

-include $(top_srcdir)/git.mk
