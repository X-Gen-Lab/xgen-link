/**
 * \file            xgl_diagnostics.h
 * \brief           Compile-time diagnostic text without changing error identity
 * \author          X-Gen Lab
 */

#ifndef XGL_DIAGNOSTICS_H
#define XGL_DIAGNOSTICS_H

#include <xgl/xgl_build_config.h>

/** \brief           Preserve a non-NULL message when diagnostic text is absent.
 */
#if XGL_FEATURE_DIAGNOSTICS
#define XGL_ERROR_MESSAGE(text) (text)
#else
#define XGL_ERROR_MESSAGE(text) ""
#endif

#endif /* XGL_DIAGNOSTICS_H */
