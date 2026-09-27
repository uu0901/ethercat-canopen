#ifndef _FOC_SVPWM_H_
#define _FOC_SVPWM_H_

#include <stdint.h>
#include <stdio.h>
#include "FOC_Math.h"

// 常量宏定义声明
#define Value_INV_SQRT3 0.5773502691896257f

// 电机相关
void SVPWM(float u_alpha, float u_beta, float *d_u, float *d_v, float *d_w);
void clarke(float *i_alpha, float *i_beta, float i_a, float i_b);
void park(float *i_d, float *i_q, float i_alpha, float i_beta, float sine, float cosine);
void ipark(float *u_alpha, float *u_beta, float u_d, float u_q, float sine, float cosine);

#endif /* _DAWNFOC_SVPWM_H_ */
