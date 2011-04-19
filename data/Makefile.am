
NULL =

desktopdir = $(datadir)/applications
desktop_in_files =					\
	goa-prefs.desktop.in				\
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.OnlineAccounts.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

pkgdata_DATA =						\
	goapanel.ui					\
	$(NULL)

EXTRA_DIST =						\
	$(desktop_in_files)				\
	$(service_in_files)				\
	$(pkgdata_DATA)					\
	dbus-interfaces.xml				\
	$(NULL)

DISTCLEANFILES =					\
	org.gnome.OnlineAccounts.service		\
	goa-prefs.desktop				\
	$(NULL)

clean-local :
	rm -f *~
