# Contributing

Thank you for considering contributing to GNOME Online Accounts!

All code contributions are made using merge requests.

This project has a [Code of Conduct](https://wiki.gnome.org/Foundation/CodeOfConduct); please,
follow it in all your interactions with members of the project and the GNOME community.

## Creating Merge Requests

To open a merge request fork the project, and then create a branch for your change.
When the change is ready, submit a merge request.

The following guidelines will help your change be successfully merged:

 * Read the [Project Goals](goals.md) before adding or requesting new service providers.
 * Keep the change as small as possible. If you can split it into multiple merge requests please do
   so.
 * Use multiple commits. This makes is easier to review and helps to diagnose bugs in the future.
 * Use clear commit messages (see below).
 * Attach screenshots of the change.
 * Link to relevant issues this change is related to.
 * Always set the merge request to allow maintainer edits - this makes it easier
   for a maintainer to make small improvements to land the change faster.

Please format commit messages as follows:

```
component: <summary>

A paragraph explaining the problem and its context.

Another one explaining how you solved that.

<link to issue(s) this change fixes>
```

## Bug Fixes

Changes that fix bugs include:

 * Correcting code that crashes.
 * Spelling mistakes in labels.
 * Small layout issues (e.g. spacing).
 * Use of deprecated APIs.
 * Restructuring of code for improved safety.

Please include clear information in the commit message(s) and merge request that indicate what is
being fixed by the change.

These changes will be reviewed for correctness, and then merged once this is complete.

## Service Providers

The [Project Goals](goals.md) details the criteria for new service providers being added.

As a part of your merge request, you must document the Terms of Service and any restrictions
or Rate Limits in [`PROVIDER_POLICIES.md`](PROVIDER_POLICIES.md). Any and all API keys, client IDs
and account credentials must be setup in coordination with the [Infrastructure Team], who will
administrate access to the account.

[Infrastructure Team]: https://gitlab.gnome.org/Infrastructure/Infrastructure

## User Experience Changes

Changes that impact the user experience require approval from the
[Design Team][design-team]. This includes:

 * Addition or removal of features in existing panels.
 * Changes to the layout of panels.
 * New panels.

Please include before/after screenshots in the merge request to show what is being changed.

For one of these changes to be merged one of the following is required:

 * The change is shown in an existing mockup done by the design team (linked to in the merge request
   or issue).
 * A comment from a design team member in the merge request or issue approving the change.

If a merge request is opened without the above the "Needs Design" label attached to it and will not
be merged until design approval is received.

Once design approval is obtained, the change will be reviewed for correctness, and then merged once
this is complete.
If design approval is not obtained, the merge request will be closed.

[design-team]: https://gitlab.gnome.org/Teams/Design

## Reviews

All changes require approval from one or more maintainers.
Reviews are likely to ask for changes to be made, please respond to these as soon as you are able
and ask for clarification if the requests are not clear.

When the change is ready to land a maintainer will mark it as approved.
Then the change can be merged by either a maintainer or the submitter if they have suitable
permissions.

## Draft Merge Requests

Merge requests marked as draft will not be reviewed by maintainers or merged.
When the change is ready for review please mark the merge request as ready.

## Inactive Merge Requests

If a merge request has comments from maintainers that have not been responded to within 4 weeks this
merge request is considered to be inactive and will be closed. The reporter may re-open it at a
later date if they respond to the comments.
