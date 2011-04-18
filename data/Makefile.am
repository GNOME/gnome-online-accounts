
NULL =

desktopdir = $(datadir)/applications
desktop_in_files =					\
	goa-prefs.desktop.in				\
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

pkgdata_DATA =						\
	goapanel.ui					\
	$(NULL)

EXTRA_DIST =						\
	$(desktop_in_files)				\
	$(pkgdata_DATA)					\
	dbus-interfaces.xml				\
	$(NULL)

DISTCLEANFILES =					\
	goa-prefs.desktop				\
	$(NULL)

clean-local :
	rm -f *~
