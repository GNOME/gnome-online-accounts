/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#ifndef __GOA_H__
#define __GOA_H__

#if !defined(GOA_API_IS_SUBJECT_TO_CHANGE) && !defined(GOA_COMPILATION)
#error  libgoa is unstable API. You must define GOA_API_IS_SUBJECT_TO_CHANGE before including goa/goa.h
#endif

#define __GOA_INSIDE_GOA_H__
#include <goa/goaclient.h>
#include <goa/goaenums.h>
#include <goa/goaerror.h>
#include <goa/goaversion.h>
#include <goa/goa-generated.h>
#undef __GOA_INSIDE_GOA_H__

#endif /* __GOA_H__ */
