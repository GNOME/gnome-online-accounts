Title: Services

## Supported providers and services

| Provider           | Mail | Calendar | Contacts | Maps | Photos | Files | Ticketing | Printers | Music |
|--------------------|------|----------|----------|------|--------|-------|-----------|----------|-------|
| Google             | yes  | yes      | yes      | no   | yes    | yes   | no        | yes      | no    |
| Microsoft Personal | yes  | no       | no       | no   | no     | no    | no        | no       | no    |
| Microsoft Exchange | yes  | yes      | yes      | no   | no     | no    | no        | no       | no    |
| NextCloud          | no   | yes      | yes      | no   | no     | yes   | no        | no       | no    |
| IMAP/SMTP          | yes  | no       | no       | no   | no     | no    | no        | no       | no    |
| Kerberos           | no   | no       | no       | no   | no     | no    | yes       | no       | no    |
| Last.fm            | no   | no       | no       | no   | no     | no    | no        | no       | yes   |

## API Keys

The list of API keys used in GNOME is available [on the wiki](https://wiki.gnome.org/Initiatives/OnlineServicesAPIKeys).

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
