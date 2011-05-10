/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include <stdlib.h>

#include "goabackendimapauth.h"

/**
 * SECTION:goabackendimapauth
 * @title: GoaBackendImapAuth
 * @short_description: Helper type for authenticating IMAP connections
 *
 * #GoaBackendImapAuth is an abstract type used for authenticating
 * IMAP connections. See #GoaBackendImapAuthOAuth for a concrete
 * implementation.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendImapAuth, goa_backend_imap_auth, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_auth_init (GoaBackendImapAuth *client)
{
}

static void
goa_backend_imap_auth_class_init (GoaBackendImapAuthClass *klass)
{
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_auth_run_sync:
 * @auth: A #GoaBackendImapAuth.
 * @input: A valid #GDataInputStream.
 * @output: A valid #GDataOutputStream.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Authenticates the IMAP connection represented by @input and
 * @output. This method blocks the calling thread until authentication
 * is done.
 *
 * Returns: %TRUE if authentication succeeded, %FALSE if @error is
 * set.
 */
gboolean
goa_backend_imap_auth_run_sync (GoaBackendImapAuth  *auth,
                                GDataInputStream    *input,
                                GDataOutputStream   *output,
                                GCancellable        *cancellable,
                                GError             **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_AUTH (auth), FALSE);
  g_return_val_if_fail (G_IS_DATA_INPUT_STREAM (input), FALSE);
  g_return_val_if_fail (G_IS_DATA_OUTPUT_STREAM (output), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_BACKEND_IMAP_AUTH_GET_CLASS (auth)->run_sync (auth, input, output, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */
