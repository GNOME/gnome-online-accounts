
NULL =

SUBDIRS = icons

gsettings_in_files = org.gnome.online-accounts.gschema.xml.in

if BUILD_BACKEND
gsettings_SCHEMAS = $(gsettings_in_files:.xml.in=.xml)
@INTLTOOL_XML_RULE@
@GSETTINGS_RULES@

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.OnlineAccounts.service.in

if BUILD_KERBEROS
service_in_files += org.gnome.Identity.service.in
endif

service_DATA     = $(service_in_files:.service.in=.service)
%.service: %.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@
endif

EXTRA_DIST =						\
	$(gsettings_in_files)				\
	dbus-interfaces.xml				\
	org.gnome.Identity.service.in			\
	org.gnome.OnlineAccounts.service.in		\
	$(NULL)

DISTCLEANFILES =					\
	$(gsettings_SCHEMAS)				\
	org.gnome.OnlineAccounts.service		\
	org.gnome.Identity.service			\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
