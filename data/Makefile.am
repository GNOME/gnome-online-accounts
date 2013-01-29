
NULL =

SUBDIRS = icons

cssdir = $(pkgdatadir)
css_DATA = goawebview.css

desktopdir = $(datadir)/applications
desktop_in_in_files = goa-daemon.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.OnlineAccounts.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

$(desktop_in_files): $(desktop_in_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =						\
	$(css_DATA)					\
	$(desktop_in_in_files)				\
	$(service_in_files)				\
	dbus-interfaces.xml				\
	$(NULL)

DISTCLEANFILES =					\
	org.gnome.OnlineAccounts.service		\
	$(desktop_DATA)					\
	$(desktop_in_files)				\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
