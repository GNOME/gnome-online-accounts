Title: Overview

## Writing GOA applications

The term *GOA application* is used to describe applications or libraries that
are using the <link linkend="ref-dbus">GNOME Online Accounts D-Bus APIs</link>
either directly or through the supplied client library.

### Account Objects

A GOA application typically creates [class@Goa.Client] object to get a
list of accounts and listen for changes. Each account provides one or more
specific services (such as [iface@Goa.Mail], [iface@Goa.Calendar],
[iface@Goa.Contacts]) so the application will need to set up a filter for the
services it is interested in. For example, a mail application would only be
interested in GOA accounts with mail- and contacts-services, not accounts with
calendaring-services. Applications can use methods on the [iface@Goa.Object]
type (such as [method@Goa.Object.peek_mail]) to check what kind of services an
account provides. Note that this list can change at run-time if e.g. the user
toggles a <guilabel>Use account for Mail</guilabel> check-button.

Applications may use the [property@Goa.Account:id] property as a unique key to
store information obtained from an account.

Applications must not destroy data if an account object is removed (e.g. when
the [signal@Goa.Client::account-removed] signal is emitted) - for example, if
the `goa-daemon` program crashes or is restarted on software upgrade, account
objects will be removed only to be added back the next time `goa-daemon` is
started.

Applications should use the [property@Goa.Account:provider-icon],
[property@Goa.Account:provider-name] and
[property@Goa.Account:presentation-identity] properties when presenting an
account in an user interface. For example, for a hypothetical online services
provider Acme, this would be the Acme Logo, the word "Acme" and the identity
could be either an email address such as `some.name@acme.com` or an user handle
such as `davidz25`.

### Accessing Services

A GOA application needs to handle service-specific protocols and quirks itself.
Some service-types, such as [iface@Goa.Mail], may use standard protocols such
as [IMAP](http://tools.ietf.org/html/rfc3501) or
[SMTP](http://tools.ietf.org/html/rfc5321) but they also may not. GOA
applications should always look at the [property@Goa.Account:provider-type]
property to infer what kind of protocol and endpoint to use.

### Credentials Handling

For accessing a service, the application typically needs to present credentials.
The application can request the credentials via GOA. First the application
should invoke the [method@Goa.Account.call_ensure_credentials] method on the
account object. If this succeeds, the application can request the credentials
using e.g. [method@Goa.OAuthBased.call_get_access_token] or
[method@Goa.OAuth2Based.call_get_access_token] depending on what kind of
credentials the account is using. If the service returns an authorization error
(say, the access token expired), the application should call
[method@Goa.Account.call_ensure_credentials] again to e.g. renew the
credentials.

On the other hand, if [method@Goa.Account.call_ensure_credentials] ever fails,
then the user will get notified that there is a problem with the account so he
can take actions to fix it. Applications can listen to changes on the
[property@Goa.Account:attention-needed] property to get notified when it's time
to try accessing the account again.

Note that the implementation for a provider may switch from e.g. OAuth to OAuth2
in a newer release of GNOME Online Accounts. Therefore, applications must never
assume that the [iface@Goa.Object] for an account implements a fixed set of
interfaces.

