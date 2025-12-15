/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Sophgo CV1800B AIAO (I2S/TDM) mux bindings
 *
 * mux index = signal * CV1800B_TDM_MAX + tdm
 * mux specifier: <&aiao_mux index state>
 */

#ifndef _DT_BINDINGS_SOUND_CV1800B_AIAO_H
#define _DT_BINDINGS_SOUND_CV1800B_AIAO_H

/* number of TDM blocks */
#define CV1800B_TDM_MAX 4

/*
 * Signal blocks
 * Must match enum cv1800b_sig order in driver
 * and reg_field block order.
 */
#define CV1800B_SIG_SCLK_IN 0
#define CV1800B_SIG_FS_IN 1
#define CV1800B_SIG_SDI_IN 2
#define CV1800B_SIG_SDO_OUT 3

#define CV1800B_AIAO_MUX(sig, tdm) ((sig) * CV1800B_TDM_MAX + (tdm))

#define CV1800B_AIAO_SCLK(tdm) CV1800B_AIAO_MUX(CV1800B_SIG_SCLK_IN, (tdm))
#define CV1800B_AIAO_FS(tdm) CV1800B_AIAO_MUX(CV1800B_SIG_FS_IN, (tdm))
#define CV1800B_AIAO_SDI(tdm) CV1800B_AIAO_MUX(CV1800B_SIG_SDI_IN, (tdm))
#define CV1800B_AIAO_SDO(tdm) CV1800B_AIAO_MUX(CV1800B_SIG_SDO_OUT, (tdm))

#endif /* _DT_BINDINGS_SOUND_CV1800B_AIAO_H */
