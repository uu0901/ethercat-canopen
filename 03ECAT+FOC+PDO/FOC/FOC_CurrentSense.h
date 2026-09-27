#ifndef FOC_CURRENT_SENSE_H
#define FOC_CURRENT_SENSE_H

/*
 * 三相电流重构实验开关：
 * 1U：丢弃当前PWM周期中低侧最先关断的一相，由另外两相重构（正常运行推荐）。
 * 0U：不重构，Ia/Ib/Ic直接使用三路ADC结果（只建议用于电压开环对比）。
 */
#ifndef FOC_CURRENT_RECONSTRUCTION_ENABLE
#define FOC_CURRENT_RECONSTRUCTION_ENABLE  1U
#endif

/* TIM1 runs at 170 MHz, ARR=4249 and center-aligned PWM=20 kHz.
 * The board uses three 20 mOhm low-side shunts followed by three amplifiers.
 * ADC1 converts IC/IB/IA sequentially.  The trigger is delayed from CNT=0
 * to avoid switching-edge noise; at high modulation the earliest-off phase
 * is discarded and reconstructed from the other two valid shunt currents. */
#define FOC_ADC_TRIGGER_TICKS          250U

#endif
