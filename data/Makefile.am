
NULL =

SUBDIRS = icons

cssdir = $(pkgdatadir)
css_DATA = goawebview.css

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.OnlineAccounts.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST =						\
	$(css_DATA)					\
	$(service_in_files)				\
	dbus-interfaces.xml				\
	$(NULL)

DISTCLEANFILES =					\
	org.gnome.OnlineAccounts.service		\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
