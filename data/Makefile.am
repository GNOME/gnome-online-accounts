
NULL =

SUBDIRS = icons

cssdir = $(pkgdatadir)
css_DATA = goawebview.css

gsettings_in_files = org.gnome.online-accounts.gschema.xml.in
gsettings_SCHEMAS = $(gsettings_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@
@GSETTINGS_RULES@

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.OnlineAccounts.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST =						\
	$(css_DATA)					\
	$(gsettings_in_files)				\
	$(service_in_files)				\
	dbus-interfaces.xml				\
	$(NULL)

DISTCLEANFILES =					\
	$(gsettings_SCHEMAS)				\
	org.gnome.OnlineAccounts.service		\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
