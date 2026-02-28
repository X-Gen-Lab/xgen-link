/**
 * \file            xgl_rtt.c
 * \brief           RTT estimator implementation
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-02-28
 *
 * \copyright       Copyright (c) 2026 Nexus Team
 *
 * \details         Implements RFC 6298 RTT estimation algorithm for adaptive
 *                  retransmission timeout calculation. Uses exponential moving
 *                  average to smooth RTT measurements and calculate RTO.
 */

#include <xgl/xgl_rtt.h>
#include <stddef.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Calculate absolute value of integer
 * \param[in]       value: Input value
 * \return          Absolute value
 */
static inline int32_t abs_int32(int32_t value) {
    return (value < 0) ? -value : value;
}

/**
 * \brief           Clamp value to range [min, max]
 * \param[in]       value: Input value
 * \param[in]       min: Minimum value
 * \param[in]       max: Maximum value
 * \return          Clamped value
 */
static inline int32_t clamp_int32(int32_t value, int32_t min, int32_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/*---------------------------------------------------------------------------*/
/* RTT Estimator Implementation                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize RTT estimator
 * \details         Sets estimator to uninitialized state with default RTO
 */
void xgl_rtt_init(xgl_rtt_estimator_t* est) {
    if (est == NULL) {
        return;
    }
    
    est->srtt = 0;
    est->rttvar = 0;
    est->rto = XGL_DEFAULT_RTO_MS;
    est->initialized = false;
}

/**
 * \brief           Update RTT estimate with new measurement
 * \details         Implements RFC 6298 algorithm:
 *                  - First measurement: SRTT = R, RTTVAR = R/2
 *                  - Subsequent measurements:
 *                    error = R - SRTT
 *                    SRTT += error/8
 *                    RTTVAR += (|error| - RTTVAR)/4
 *                  - RTO = SRTT + 4 * RTTVAR
 *                  - Clamp RTO to [MIN_RTO, MAX_RTO]
 * \note            Handles negative RTT measurements by clamping to 0
 */
void xgl_rtt_update(xgl_rtt_estimator_t* est, int32_t measured_rtt) {
    if (est == NULL) {
        return;
    }
    
    /* Clamp measured RTT to non-negative value */
    if (measured_rtt < 0) {
        measured_rtt = 0;
    }
    
    if (!est->initialized) {
        /* First measurement (RFC 6298 Section 2.2) */
        est->srtt = measured_rtt;
        est->rttvar = measured_rtt >> 1;  /* RTTVAR = R/2 */
        est->initialized = true;
    } else {
        /* Subsequent measurements (RFC 6298 Section 2.3) */
        int32_t error = measured_rtt - est->srtt;
        
        /* SRTT = SRTT + error/8 (alpha = 1/8) */
        est->srtt += (error >> XGL_RTT_ALPHA_SHIFT);
        
        /* RTTVAR = RTTVAR + (|error| - RTTVAR)/4 (beta = 1/4) */
        int32_t abs_error = abs_int32(error);
        int32_t rttvar_delta = abs_error - est->rttvar;
        est->rttvar += (rttvar_delta >> XGL_RTT_BETA_SHIFT);
    }
    
    /* Calculate RTO = SRTT + 4 * RTTVAR (RFC 6298 Section 2.4) */
    est->rto = est->srtt + (XGL_RTO_K_FACTOR * est->rttvar);
    
    /* Clamp RTO to [MIN_RTO, MAX_RTO] (RFC 6298 Section 2.4) */
    est->rto = clamp_int32(est->rto, XGL_MIN_RTO_MS, XGL_MAX_RTO_MS);
}

/**
 * \brief           Get current RTO value
 * \details         Returns default RTO if not initialized
 */
int32_t xgl_rtt_get_rto(const xgl_rtt_estimator_t* est) {
    if (est == NULL) {
        return XGL_DEFAULT_RTO_MS;
    }
    
    return est->rto;
}

/**
 * \brief           Get current SRTT value
 * \details         Returns 0 if not initialized
 */
int32_t xgl_rtt_get_srtt(const xgl_rtt_estimator_t* est) {
    if (est == NULL || !est->initialized) {
        return 0;
    }
    
    return est->srtt;
}

/**
 * \brief           Get current RTTVAR value
 * \details         Returns 0 if not initialized
 */
int32_t xgl_rtt_get_rttvar(const xgl_rtt_estimator_t* est) {
    if (est == NULL || !est->initialized) {
        return 0;
    }
    
    return est->rttvar;
}

/**
 * \brief           Reset RTT estimator to initial state
 * \details         Clears all measurements and returns to uninitialized state
 */
void xgl_rtt_reset(xgl_rtt_estimator_t* est) {
    xgl_rtt_init(est);
}

/**
 * \brief           Check if RTT estimator is initialized
 * \details         Returns true if at least one measurement has been processed
 */
bool xgl_rtt_is_initialized(const xgl_rtt_estimator_t* est) {
    if (est == NULL) {
        return false;
    }
    
    return est->initialized;
}
