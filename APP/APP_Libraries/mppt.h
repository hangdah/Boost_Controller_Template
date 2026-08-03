/*
 * mppt.h
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#ifndef APP_APP_LIBRARIES_MPPT_H_
#define APP_APP_LIBRARIES_MPPT_H_

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"

typedef struct {
    float  Ipv;
    float  Vpv;
    float  MaxI;
    float  MinI;
    float  Stepsize;
    float  ImppOut;
    // internal variables
    float  Step;
    float  PpvOld;
    float  IpvOld;
    int mppt_first;
    int mppt_enable;
} uinv_dcdc_mppt_t_I;

typedef struct {
    float  Ipv;
    float  Vpv;
    float  MaxV;
    float  MinV;
    float  Stepsize;
    float  VmppOut;
    // internal variables
    float  Step;
    float  PpvOld;
    float  VpvOld;
    int mppt_first;
    int mppt_enable;
} uinv_dcdc_mppt_t_V;

void uinv_dcdc_mppt_init_I(uinv_dcdc_mppt_t_I *v);
void uinv_dcdc_mppt_init_V(uinv_dcdc_mppt_t_V *v);
void uinv_dcdc_mppt_run_Impp(uinv_dcdc_mppt_t_I *v);
void uinv_dcdc_mppt_run_Vmpp(uinv_dcdc_mppt_t_V *v);




#endif /* APP_APP_LIBRARIES_MPPT_H_ */
