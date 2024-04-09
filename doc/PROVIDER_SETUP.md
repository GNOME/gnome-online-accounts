# Provider Setup

This page holds instructions and helpful links for providers that may need
some setup to be performed by the user. For example, some providers require app
passwords for specific services.

If you notice any incorrect or out-of-date information here, please don't
hesitate to open a merge request or issue.

## WebDAV

### Fastmail

Fastmail's DAV services **require** different app passwords for WebDAV (file)
and CalDAV/CardDAV (calendar/contacts). Additionally, CardDAV services use a
different username for each addressbook.

Start by logging into your Fastmail account so you can setup app passwords for
the services you want to setup. Note that in GNOME 46, each service will require
a separate account in GNOME Online Accounts.

**Official Documentation**
- [Fastmail: Server names and ports](https://www.fastmail.help/hc/en-us/articles/1500000278342-Server-names-and-ports)
- [Fastmail: Remote file access](https://www.fastmail.help/hc/en-us/articles/1500000277882-Remote-file-access)
- [Fastmail: App passwords](https://www.fastmail.help/hc/en-us/articles/360058752854-App-passwords)

#### WebDAV (Files)

File access is fairly straight-forward, and only needs an app password. Note
that `username@domain.tld` will usually be `username@fastmail.com`, unless you
have a custom domain setup with Fastmail.

| Field          | Value                                           |
| -------------- | ----------------------------------------------- |
| Server Address | `https://myfiles.fastmail.com/`                 |
| Username       | Your Email address (e.g. `username@domain.tld`) |
| Password       | Your WebDAV app password                        |

#### CalDAV (Calendars)

Calendar access is similar to files access, and only needs an app password. 

| Field          | Value                                           |
| -------------- | ----------------------------------------------- |
| Server Address | `https://caldav.fastmail.com/`                  |
| Username       | Your Email address (e.g. `username@domain.tld`) |
| Password       | Your CalDAV app password                        |

#### CardDAV (Contacts)

Contact access needs an app password, but also has a different username for
each addressbook. You can use the same app password for all addressbooks.

If you only want the *"Default"* addressbook, you may use the values below:

| Field          | Value                                           |
| -------------- | ----------------------------------------------- |
| Server Address | `https://carddav.fastmail.com/`                 |
| Username       | Your Email address (e.g. `username@domain.tld`) |
| Password       | Your CardDAV app password                       |

To access other addressbooks you must specify the username and full URL. For
example, to access the *"Shared"* addressbook use the values below:

| Field          | Value                                           |
| -------------- | ----------------------------------------------- |
| Server Address | `https://carddav.fastmail.com/dav/addressbooks/user/username@domain.tld/mainuser@domain.tld.Shared` |
| Username       | `username+Shared@domain.tld`                    |
| Password       | Your CardDAV app password                       |

Note that in the *Server Address* URL, `username@domain.tld` is the *accessing*
user, while `mainuser@domain.tld` is the *owner* of the addressbook. For most
users this will be the same email address.

### mailbox.org

mailbox.org DAV services support using your regular password, or different app
passwords for for WebDAV (files) and CalDAV/CardDAV (calendar/contacts).

When using an app password the account may indicate that it supports other
services, but later fail when an application tries to access them. You can turn
these features off with the account's details dialog in GNOME Settings.

It's recommended to use your main password, if possible. In this case you can
use the values below:

| Field          | Value                                            |
| -------------- | ------------------------------------------------ |
| Server Address | `https://dav.mailbox.org/`                       |
| Username       | Your Email address (e.g. `username@mailbox.org`) |
| Password       | Your Email password                              |

**Offical Documentation**
- [mailbox.org: WebDAV for Linux](https://kb.mailbox.org/en/private/drive-article/webdav-for-linux/)
- [mailbox.org: CalDAV and CardDAV for other devices](https://kb.mailbox.org/en/private/calendar-article/caldav-and-carddav-for-other-devices)

#### WebDAV (Files)

To use an app password for files access, you may use values below:

| Field          | Value                                               |
| -------------- | --------------------------------------------------- |
| Server Address | `https://dav.mailbox.org/servlet/webdav.infostore/` |
| Username       | Your Email address (e.g. `username@mailbox.org`)    |
| Password       | Your WebDAV app password                            |

#### CalDAV (Calendars)

To use an app password for calendars, you may use values below:

| Field          | Value                                               |
| -------------- | --------------------------------------------------- |
| Server Address | `https://dav.mailbox.org/.well-known/caldav`        |
| Username       | Your Email address (e.g. `username@mailbox.org`)    |
| Password       | Your CalDAV app password                            |

#### CardDAV (Contacts)

To use an app password for contacts, you may use values below:

| Field          | Value                                               |
| -------------- | --------------------------------------------------- |
| Server Address | `https://dav.mailbox.org/.well-known/carddav`       |
| Username       | Your Email address (e.g. `username@mailbox.org`)    |
| Password       | Your CardDAV app password                           |

