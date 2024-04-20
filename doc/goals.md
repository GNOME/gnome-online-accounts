Title: Goals

GNOME Online Accounts aims to provide a way for users to setup online accounts
to be used by *the core system and applications*.

System usage of online accounts includes GNOME Shell's calendar, cloud file
storage in file selection dialogs (through GIO), cloud printing and Kerberos
credentials. Core application integration includes things like Calendar,
Contacts, Files and Photos.

*While third-party applications can access the accounts setup through GNOME
Online Accounts today, maintainers of third-party applications should contact
the GNOME Online Accounts maintainers as well as the GNOME design and release
team first*.

Accounts offered through GNOME Online Accounts should appeal to a wide range
of users.

## Account policy

Online Accounts uses a predefined list of online accounts that users can setup.
Accounts are included that conform to the following policy:

- Must be used by the core GNOME system and apps
- Should ideally provide multiple services that can be consumed by multiple
  apps and services
- Should provide generic, common functionality that is readily recognised by
  users 

As a rule, specialist single-purpose accounts are avoided. Accounts which are
only consumed by non-core or non-GNOME apps are similarly avoided.

The following principles inform this policy:

- Protect GNOME's access to online services. Applications using Online Accounts
  use GNOME's profile to access those online accounts. It is the GNOME project's
  responsibility to ensure that any access through its profiles follows the
  account provider's terms and conditions. Breaching those terms and conditions
  could result in GNOME's access being revoked. It is therefore important that
  GNOME restricts access to its profiles, in order to mitigate any risk of this
  happening.
- Maintain the identity of third party applications. Applications which have
  their own brands and identities ought to be exposed to services and users
  using those identities, and should not be falsely advertised as "GNOME".
- Ensure that online accounts are readily understood by users. A long list of
  specialised account types would become burdensome for users.
- Ensure that setting up an account has value for users. If a user sets up an
  account, the services it provides ought to be accessible in obvious and
  useful ways. People shouldn't be left wondering what the point was.
- Work towards the goal of application sandboxing, in order to protect user
  privacy and security. 
