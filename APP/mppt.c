/*
 * mppt.c
 *
 *  Created on: 2025年8月5日
 *      Author: da
 */

#include "mppt.h"

void uinv_dcdc_mppt_init_I(uinv_dcdc_mppt_t_I *v) {
    v->mppt_first = 1;
}

void uinv_dcdc_mppt_init_V(uinv_dcdc_mppt_t_V *v) {
    v->mppt_first = 1;
}

void uinv_dcdc_mppt_run_Impp(uinv_dcdc_mppt_t_I *v) {
    float P = v->Vpv * v->Ipv;

    if(v->mppt_enable) {
        if(v->mppt_first) {
            v->ImppOut= v->Ipv;
            v->PpvOld = P;
            v->IpvOld = v->Ipv;
            v->mppt_first = 0;
        } else {
            float deltaI = v->Ipv - v->IpvOld;

            if(deltaI > 0) { // If we are increasing current
                if(P > v->PpvOld) { // and power increases
                    v->ImppOut=v->Ipv+v->Stepsize;
                } else { // and power decreases
                    v->ImppOut=v->Ipv-v->Stepsize;
                }
            } else { // If we are decreasing current
                if(P > v->PpvOld) { // and power increases
                    v->ImppOut=v->Ipv-v->Stepsize;
                } else { // and power decreases
                    v->ImppOut=v->Ipv+v->Stepsize;
                }
            }
        }
    }
    v->ImppOut=(v->ImppOut<v->MinI) ? v->MinI : v->ImppOut;
    v->ImppOut=(v->ImppOut>v->MaxI) ? v->MaxI : v->ImppOut;

    v->IpvOld = v->Ipv;
    v->PpvOld = P;
}

void uinv_dcdc_mppt_run_Vmpp(uinv_dcdc_mppt_t_V *v) {
    float P = v->Vpv * v->Ipv;

    if(v->mppt_enable) {
        if(v->mppt_first) {
            v->VmppOut = v->Vpv;
            v->PpvOld = P;
            v->VpvOld = v->Vpv;
            v->mppt_first = 0;
        } else {
            float deltaV = v->Vpv - v->VpvOld;

            if(deltaV > 0) { // 电压增加
                if(P > v->PpvOld) { // 功率增加
                    v->VmppOut = v->Vpv + v->Stepsize;
                } else {
                    v->VmppOut = v->Vpv - v->Stepsize;
                }
            } else { // 电压减少
                if(P > v->PpvOld) {
                    v->VmppOut = v->Vpv - v->Stepsize;
                } else {
                    v->VmppOut = v->Vpv + v->Stepsize;
                }
            }
        }
    }

    v->VmppOut = (v->VmppOut < v->MinV) ? v->MinV : v->VmppOut;
    v->VmppOut = (v->VmppOut > v->MaxV) ? v->MaxV : v->VmppOut;

    v->VpvOld = v->Vpv;
    v->PpvOld = P;
}


