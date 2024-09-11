Title: Services

## Supported Providers and Services

| Provider           | Mail | Calendar | Contacts | Maps | Photos | Files | Ticketing | Printers | Music |
|--------------------|------|----------|----------|------|--------|-------|-----------|----------|-------|
| Google             | yes  | yes      | yes      | no   | no     | yes   | no        | no       | no    |
| IMAP/SMTP          | yes  | no       | no       | no   | no     | no    | no        | no       | no    |
| Microsoft          | yes  | no       | no       | no   | no     | no    | no        | no       | no    |
| Microsoft 365      | yes  | yes      | yes      | no   | no     | yes   | no        | no       | no    |
| Microsoft Exchange | yes  | yes      | yes      | no   | no     | no    | no        | no       | no    |
| WebDAV             | no   | yes      | yes      | no   | no     | yes   | no        | no       | no    |
| NextCloud (WebDAV) | no   | yes      | yes      | no   | no     | yes   | no        | no       | no    |
| Kerberos           | no   | no       | no       | no   | no     | no    | yes       | no       | no    |
| Fedora (Kerberos)  | no   | no       | no       | no   | no     | no    | yes       | no       | no    |

## Provider Administration

GNOME maintains a number of accounts with associated API keys and credentials.
Access to the accounts is usually administrated by the [GNOME Infrastructure]
team, although some older accounts may be held by GNOME Foundation members.

| Provider           | Contact                            | Added      | Removed    |
|--------------------|------------------------------------|------------|------------|
| Facebook           | [Debarshi Ray], [Xavier Claessens] | GNOME 3.4  | GNOME 43   |
| Flickr             | [Debarshi Ray]                     | GNOME 3.8  | GNOME 43   |
| Foursquare         | [Marcus Lundblad]                  | GNOME 3.16 | GNOME 43   |
| Google             | [GNOME Infrastructure]             | GNOME 3.2  |            |
| Last.fm            | [Felipe Borges]                    | GNOME 3.18 | GNOME 46   |
| Microsoft          | [GNOME Infrastructure]             | GNOME 46   |            |
| Microsoft (legacy) | [Debarshi Ray]                     | GNOME 3.4  | GNOME 46   |
| Pocket             | [Bastien Nocera]                   | GNOME 3.12 | GNOME 3.36 |

[GNOME Infrastructure]: https://gitlab.gnome.org/Infrastructure/Infrastructure
[Debarshi Ray]: https://gitlab.gnome.org/debarshir
[Xavier Claessens]: https://gitlab.gnome.org/xclaessens
[Marcus Lundblad]: https://gitlab.gnome.org/mlundblad
[Felipe Borges]: https://gitlab.gnome.org/felipeborges
[Bastien Nocera]: https://gitlab.gnome.org/hadess

## Google

### OAuth 2.0

- https://developers.google.com/accounts/docs/OAuth2InstalledApp
- https://developers.google.com/apis-explorer/
- https://developers.google.com/oauthplayground/

### Scopes

- https://developers.google.com/accounts/docs/OAuth2Login
- https://developers.google.com/google-apps/calendar/auth
- https://developers.google.com/google-apps/contacts/v3/
- https://developers.google.com/drive/web/scopes
- https://developers.google.com/google-apps/gmail/oauth_protocol
- https://developers.google.com/picasa-web/docs/2.0/developers_guide_protocol
- https://developers.google.com/talk/jep_extensions/oauth
- https://developers.google.com/cloud-print/docs/devguide

Sometimes the documentation is lacking. In such cases, the following can be
useful:

- https://developers.google.com/apis-explorer/
- https://developers.google.com/oauthplayground/
- https://discovery-check.appspot.com/

### Notes

We are allowed to embed the client_secret in the source code. See
https://developers.google.com/accounts/docs/OAuth2InstalledApp#overview

## Windows Live

### OAuth 2.0

- http://msdn.microsoft.com/en-us/library/live/hh243647.aspx

### Scopes

- http://msdn.microsoft.com/en-us/library/live/hh243646.aspx
- http://blogs.office.com/b/microsoft-outlook/archive/2013/09/12/outlook-com-now-with-imap.aspx

### Notes

We do not need the client_secret because we are marked as a desktop or mobile
application, and we use https://login.live.com/oauth20_desktop.srf as the
redirect_uri.
