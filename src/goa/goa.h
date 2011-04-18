#ifndef __GOA_H__
#define __GOA_H__

#if !defined(GOA_API_IS_SUBJECT_TO_CHANGE) && !defined(GOA_COMPILATION)
#error  libgoa is unstable API. You must define GOA_API_IS_SUBJECT_TO_CHANGE before including goa/goa.h
#endif

#define __GOA_INSIDE_GOA_H__
#include <goa/goatypes.h>
#include <goa/goaclient.h>
#include <goa/goaerror.h>
#include <goa/goa-generated.h>
#undef __GOA_INSIDE_GOA_H__

#endif /* __GOA_H__ */
