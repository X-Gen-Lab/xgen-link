/**
 * \file            xgl_rtt.h
 * \brief           RTT estimator interface
 * \author          X-Gen Lab
 */

#ifndef XGL_RTT_H
#define XGL_RTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* RTT Configuration Constants                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Minimum RTO value in milliseconds
 * \details         Prevents RTO from becoming too small
 */
#define XGL_MIN_RTO_MS          100

/**
 * \brief           Maximum RTO value in milliseconds
 * \details         Prevents RTO from becoming too large
 */
#define XGL_MAX_RTO_MS          5000

/**
 * \brief           Default RTO value in milliseconds
 * \details         Used when no RTT measurements are available
 */
#define XGL_DEFAULT_RTO_MS      1000

/**
 * \brief           RTT smoothing factor (alpha = 1/8)
 * \details         Used in SRTT calculation: SRTT += error/8
 */
#define XGL_RTT_ALPHA_SHIFT     3

/**
 * \brief           RTT variation smoothing factor (beta = 1/4)
 * \details         Used in RTTVAR calculation: RTTVAR += (|error| - RTTVAR)/4
 */
#define XGL_RTT_BETA_SHIFT      2

/**
 * \brief           RTO calculation factor (K = 4)
 * \details         RTO = SRTT + K * RTTVAR
 */
#define XGL_RTO_K_FACTOR        4

/*---------------------------------------------------------------------------*/
/* RTT Estimator Structure                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           RTT estimator structure (RFC 6298)
 * \details         Implements adaptive retransmission timeout calculation
 *                  using exponential moving average
 */
typedef struct {
    int32_t srtt;                   /**< Smoothed RTT in milliseconds */
    int32_t rttvar;                 /**< RTT variation in milliseconds */
    int32_t rto;                    /**< Retransmission timeout in milliseconds */
    bool initialized;               /**< True if first measurement received */
} xgl_rtt_estimator_t;

/*---------------------------------------------------------------------------*/
/* RTT Estimator Functions                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize RTT estimator
 * \param[out]      est: RTT estimator structure
 */
void xgl_rtt_init(xgl_rtt_estimator_t* est);

/**
 * \brief           Update RTT estimate with new measurement
 * \param[in,out]   est: RTT estimator structure
 * \param[in]       measured_rtt: Measured RTT in milliseconds
 * \details         Implements RFC 6298 algorithm:
 *                  - First measurement: SRTT = R, RTTVAR = R/2
 *                  - Subsequent: SRTT += (R - SRTT)/8, RTTVAR += (|R - SRTT| - RTTVAR)/4
 *                  - RTO = SRTT + 4 * RTTVAR, clamped to [MIN_RTO, MAX_RTO]
 */
void xgl_rtt_update(xgl_rtt_estimator_t* est, int32_t measured_rtt);

/**
 * \brief           Get current RTO value
 * \param[in]       est: RTT estimator structure
 * \return          Current RTO in milliseconds
 * \details         Returns default RTO if not initialized
 */
int32_t xgl_rtt_get_rto(const xgl_rtt_estimator_t* est);

/**
 * \brief           Get current SRTT value
 * \param[in]       est: RTT estimator structure
 * \return          Current SRTT in milliseconds, or 0 if not initialized
 */
int32_t xgl_rtt_get_srtt(const xgl_rtt_estimator_t* est);

/**
 * \brief           Get current RTTVAR value
 * \param[in]       est: RTT estimator structure
 * \return          Current RTTVAR in milliseconds, or 0 if not initialized
 */
int32_t xgl_rtt_get_rttvar(const xgl_rtt_estimator_t* est);

/**
 * \brief           Reset RTT estimator to initial state
 * \param[in,out]   est: RTT estimator structure
 */
void xgl_rtt_reset(xgl_rtt_estimator_t* est);

/**
 * \brief           Check if RTT estimator is initialized
 * \param[in]       est: RTT estimator structure
 * \return          True if initialized, false otherwise
 */
bool xgl_rtt_is_initialized(const xgl_rtt_estimator_t* est);

#ifdef __cplusplus
}
#endif

#endif /* XGL_RTT_H */
