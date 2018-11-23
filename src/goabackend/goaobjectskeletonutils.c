/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goaobjectskeletonutils.h"

void
goa_object_skeleton_attach_calendar (GoaObjectSkeleton *object,
                                     const gchar       *uri,
                                     gboolean           calendar_enabled,
                                     gboolean           accept_ssl_errors)
{
  GoaCalendar *calendar;

  calendar = goa_object_get_calendar (GOA_OBJECT (object));
  if (calendar_enabled)
    {
      if (calendar == NULL)
        {
          calendar = goa_calendar_skeleton_new ();
          g_object_set (G_OBJECT (calendar),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri,
                        NULL);
          goa_object_skeleton_set_calendar (object, calendar);
        }
    }
  else
    {
      if (calendar != NULL)
        goa_object_skeleton_set_calendar (object, NULL);
    }
  g_clear_object (&calendar);
}

void
goa_object_skeleton_attach_files (GoaObjectSkeleton *object,
                                  const gchar       *uri,
                                  gboolean           files_enabled,
                                  gboolean           accept_ssl_errors)
{
  GoaFiles *files;

  files = goa_object_get_files (GOA_OBJECT (object));
  if (files_enabled)
    {
      if (files == NULL)
        {
          files = goa_files_skeleton_new ();
          g_object_set (G_OBJECT (files),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri,
                        NULL);
          goa_object_skeleton_set_files (object, files);
        }
    }
  else
    {
      if (files != NULL)
        goa_object_skeleton_set_files (object, NULL);
    }
  g_clear_object (&files);
}

void
goa_object_skeleton_attach_contacts (GoaObjectSkeleton *object,
                                     const gchar       *uri,
                                     gboolean           contacts_enabled,
                                     gboolean           accept_ssl_errors)
{
  GoaContacts *contacts;

  contacts = goa_object_get_contacts (GOA_OBJECT (object));
  if (contacts_enabled)
    {
      if (contacts == NULL)
        {
          contacts = goa_contacts_skeleton_new ();
          g_object_set (G_OBJECT (contacts),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri,
                        NULL);
          goa_object_skeleton_set_contacts (object, contacts);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }
  g_clear_object (&contacts);
}

void
goa_object_skeleton_attach_documents (GoaObjectSkeleton *object,
                                      gboolean           documents_enabled)
{
  GoaDocuments *documents;

  documents = goa_object_get_documents (GOA_OBJECT (object));
  if (documents_enabled)
    {
      if (documents == NULL)
        {
          documents = goa_documents_skeleton_new ();
          goa_object_skeleton_set_documents (object, documents);
        }
    }
  else
    {
      if (documents != NULL)
        goa_object_skeleton_set_documents (object, NULL);
    }
  g_clear_object (&documents);
}

void
goa_object_skeleton_attach_photos (GoaObjectSkeleton *object,
                                   gboolean           photos_enabled)
{
  GoaPhotos *photos;

  photos = goa_object_get_photos (GOA_OBJECT (object));

  if (photos_enabled)
    {
      if (photos == NULL)
        {
          photos = goa_photos_skeleton_new ();
          goa_object_skeleton_set_photos (object, photos);
        }
    }
  else
    {
      if (photos != NULL)
        goa_object_skeleton_set_photos (object, NULL);
    }
  g_clear_object (&photos);
}

void
goa_object_skeleton_attach_printers (GoaObjectSkeleton *object,
                                     gboolean           printers_enabled)
{
  GoaPrinters *printers;

  printers = goa_object_get_printers (GOA_OBJECT (object));

  if (printers_enabled)
    {
      if (printers == NULL)
        {
          printers = goa_printers_skeleton_new ();
          goa_object_skeleton_set_printers (object, printers);
        }
    }
  else
    {
      if (printers != NULL)
        goa_object_skeleton_set_printers (object, NULL);
    }
  g_clear_object (&printers);
}

void
goa_object_skeleton_attach_maps (GoaObjectSkeleton *object,
                                 gboolean           maps_enabled)
{
  GoaMaps *maps;

  maps = goa_object_get_maps (GOA_OBJECT (object));

  if (maps_enabled)
    {
      if (maps == NULL)
        {
          maps = goa_maps_skeleton_new ();
          goa_object_skeleton_set_maps (object, maps);
        }
    }
  else
    {
      if (maps != NULL)
        goa_object_skeleton_set_maps (object, NULL);
    }
  g_clear_object (&maps);
}

void
goa_object_skeleton_attach_read_later (GoaObjectSkeleton *object,
                                       gboolean           read_later_enabled)
{
  GoaReadLater *readlater = NULL;

  readlater = goa_object_get_read_later (GOA_OBJECT (object));
  if (read_later_enabled)
    {
      if (readlater == NULL)
        {
          readlater = goa_read_later_skeleton_new ();
          goa_object_skeleton_set_read_later (object, readlater);
        }
    }
  else
    {
      if (readlater != NULL)
        goa_object_skeleton_set_read_later (object, NULL);
    }
  g_clear_object (&readlater);
}
