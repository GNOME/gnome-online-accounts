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

#ifndef __GOA_BACKEND_H__
#define __GOA_BACKEND_H__

#if !defined(GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE) && !defined(GOA_BACKEND_COMPILATION)
#error  libgoa-backend is unstable API. You must define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE before including goabackend/goabackend.h
#endif

#define __GOA_BACKEND_INSIDE_GOA_BACKEND_H__
#include <goabackend/goabackendenumtypes.h>
#include <goabackend/goabackendenums.h>
#include <goabackend/goaprovider.h>
#undef __GOA_BACKEND_INSIDE_GOA_BACKEND_H__

#endif /* __GOA_BACKEND_H__ */
