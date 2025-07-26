Title: Configuration

GNOME Online Accounts uses simple [KeyFile] configuration files, for both user accounts and
service configuration.

[KeyFile]: https://wikipedia.org/wiki/INI_file

## Service Configuration

Restrictions for providers and services are stored in `$SYSCONFDIR/goa.conf` (i.e. `/etc/goa.conf`).
All providers and all services are enabled by default, such that the effective default
configuration is:

```ini
[providers]
enable=all

[all]
mail=true
calendar=true
contacts=true
chat=true
documents=true
photos=true
files=true
ticketing=true
read-later=true
printers=true
maps=true
music=true
todo=true
```

### Restricting Providers

Providers can be restricted to a subset of available providers, which can not be overridden
by users or circumvented by adding new providers. The settings are read following this procedure:

1. If there is a `[providers]` group and the `enable` key is missing or set to `all`, the provider
  is enabled
2. If there is a `[providers]` group and the `enable` key includes the provider, the provider
  is enabled
3. If there are legacy GSettings<sup>[1](#legacy-gsettings)</sup>, those settings are used
4. If neither are present, the provider is enabled

The following configuration file restricts providers to those using open protocols:

```ini
[providers]
enable=imap_smtp;webdav;kerberos
```

<sup id="legacy-gsettings">1: Legacy GSettings are not documented or supported. They were
removed entirely in version 3.56.0 (GNOME 49)</sup>

### Restricting Services

Services can be enabled or disabled for all providers using the special `[all]` group, allowing
specific providers to override these settings. The settings are read following this procedure:

1. If there is a provider group with key for the service, that setting is used
2. If there is a `[all]` group with a key for the service, that setting is used
3. If neither are present, the service is enabled

The example below disables calendar, contacts and files for all providers, except `google`:

```ini
[all]
mail=true
contacts=false
calendar=false
files=false

[google]
contacts=true
calendar=true
files=true
```

## User Accounts

All accounts for a user are stored in `$XDG_CONFIG_HOME/goa-1.0/accounts.conf`, detailing the
login name and service details (e.g. hostname, port, etc). Sensitive credentials are stored in
the user's keyring (i.e. libsecret).

```ini
[Account account_1719962773_0]
Provider=google
Identity=username@gmail.com
PresentationIdentity=username@gmail.com
MailEnabled=true
CalendarEnabled=true
ContactsEnabled=true
FilesEnabled=true

[Account account_1720638412_0]
Provider=webdav
Identity=username
PresentationIdentity=username@example.com
Uri=https://webdav.example.com
CalendarEnabled=true
CalDavUri=https://caldav.example.com
ContactsEnabled=true
CardDavUri=https://carddav.example.com
FilesEnabled=true
AcceptSslErrors=false
```

Note that the format of these files is effectively private, with no stability guarantees. If
the format changes, `goa-daemon` will handle account migration and notify the user if an
account needs to be re-authenticated.

