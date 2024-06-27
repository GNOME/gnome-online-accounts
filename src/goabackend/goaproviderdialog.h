/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_PROVIDER_DIALOG_H__
#define __GOA_PROVIDER_DIALOG_H__

#include <adwaita.h>

#include "goaprovider.h"

G_BEGIN_DECLS

/**
 * GoaDialogState:
 * @GOA_DIALOG_IDLE: the dialog waiting for input
 * @GOA_DIALOG_READY: the dialog ready to proceed
 * @GOA_DIALOG_BUSY: the dialog is processing a request
 * @GOA_DIALOG_ERROR: the dialog is reporting an error
 * @GOA_DIALOG_DONE: the dialog has reached a terminal state
 *
 * Enumeration of dialog states.
 *
 * See the documentation for [class@Goa.ProviderDialog] for details on using
 * these states in a [class@Goa.Provider] implementation.
 */
typedef enum
{
  GOA_DIALOG_IDLE,
  GOA_DIALOG_READY,
  GOA_DIALOG_BUSY,
  GOA_DIALOG_ERROR,
  GOA_DIALOG_DONE,
} GoaDialogState;

#define GOA_TYPE_PROVIDER_DIALOG (goa_provider_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GoaProviderDialog, goa_provider_dialog, GOA, PROVIDER_DIALOG, AdwDialog)

GoaProviderDialog   *goa_provider_dialog_new                (GoaProvider       *provider,
                                                             GoaClient         *client,
                                                             GtkWidget         *parent);
GoaProviderDialog   *goa_provider_dialog_new_full           (GoaProvider       *provider,
                                                             GoaClient         *client,
                                                             GtkWidget         *parent,
                                                             int                default_width,
                                                             int                default_height);
GoaClient           *goa_provider_dialog_get_client         (GoaProviderDialog *self);
GoaProvider         *goa_provider_dialog_get_provider       (GoaProviderDialog *self);
GoaDialogState       goa_provider_dialog_get_state          (GoaProviderDialog *self);
void                 goa_provider_dialog_set_state          (GoaProviderDialog *self,
                                                             GoaDialogState     state);
void                 goa_provider_dialog_push_account       (GoaProviderDialog *self,
                                                             GoaObject         *object,
                                                             GtkWidget         *content);
GtkWidget           *goa_provider_dialog_push_content       (GoaProviderDialog *self,
                                                             const char        *title,
                                                             GtkWidget         *content);
void                 goa_provider_dialog_report_error       (GoaProviderDialog *self,
                                                             const GError      *error);

/* UI Helpers */
GtkWidget           *goa_provider_dialog_add_page           (GoaProviderDialog *self,
                                                             const char        *title,
                                                             const char        *description);
GtkWidget           *goa_provider_dialog_add_group          (GoaProviderDialog *self,
                                                             const char        *title);
GtkWidget           *goa_provider_dialog_add_combo          (GoaProviderDialog *self,
                                                             GtkWidget         *group,
                                                             const char        *label,
                                                             GStrv              strings);
GtkWidget           *goa_provider_dialog_add_entry          (GoaProviderDialog *self,
                                                             GtkWidget         *group,
                                                             const char        *label);
GtkWidget           *goa_provider_dialog_add_password_entry (GoaProviderDialog *self,
                                                             GtkWidget         *group,
                                                             const char        *label);
GtkWidget           *goa_provider_dialog_add_description    (GoaProviderDialog *self,
                                                             GtkWidget         *target,
                                                             const char        *description);
void                 goa_provider_dialog_add_toast          (GoaProviderDialog *self,
                                                             AdwToast          *toast);

/* GTask Helpers */
void                 goa_provider_task_run_in_dialog        (GTask             *task,
                                                             GoaProviderDialog *dialog);
void                 goa_provider_task_return_account       (GTask             *task,
                                                             GoaObject         *object);
void                 goa_provider_task_return_error         (GTask             *task,
                                                             GError            *error);

G_END_DECLS

#endif /* __GOA_PROVIDER_DIALOG_H__ */

