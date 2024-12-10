/*******************************************************************************
* File Name: buck_protection.h
*
* Description:
* This is a user-defined header file for call back functions with protection
* conditions. These functions will get called from control loop in the generated
* code.
*******************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#ifndef BUCK_PROTECTION_H
#define BUCK_PROTECTION_H

#include "cybsp.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* buck converter states */
typedef enum Ifx_buck_states
{
    Ifx_BUCK_STATE_IDLE      = 0,
    Ifx_BUCK_STATE_RUN       = 1,
    Ifx_BUCK_STATE_TEST      = 2,
    Ifx_BUCK_STATE_FAULT     = 3
}Ifx_buck_states;

/* Number of samples for averaging the parameters used for overload protection */
#define AVERAGING_SAMPLES     (8U)

/* input voltage */
#define VIN_COUNT             (1906)       /* ADC count for input voltage - 24v*/

/* Counters values for LED operation */
#define CLR_LED    (0)
#define SET_LED    (10000)
#define TOGGLE_LED (5000)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Variables used for Protection for converter 1*/ /* buck1 */
extern float32_t buck1_vout_adc_res;
extern float32_t buck1_iout_adc_res;
extern float32_t buck1_temp_adc_res;
extern float32_t vin_adc_res;
extern float32_t buck1_vout_avg;
extern float32_t buck1_iout_avg;
extern float32_t buck1_temp_avg;
extern float32_t vin_avg;
extern uint32_t buck1_enable_protection;

/* Variables used for Protection for converter 2*/ /* buck2 */
extern float32_t buck2_vout_adc_res;
extern float32_t buck2_iout_adc_res;
extern float32_t buck2_temp_adc_res;
extern float32_t buck2_vout_avg;
extern float32_t buck2_iout_avg;
extern float32_t buck2_temp_avg;
extern uint32_t buck2_enable_protection;

/* state variable */
extern Ifx_buck_states buck_state;

/* Interrupt configuration structure of button GPIO. */
extern cy_stc_sysint_t button_press_intr_config;

/*******************************************************************************
* Function Name: fault_processing
*********************************************************************************
* Summary:
* This function is executes when a fault is detected. It disables
* the buck converter and changes the state.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
__STATIC_INLINE void fault_processing(void)
{
    /* Disable the buck converter when protection condition passed. */
    BUCK1_disable(); /* buck1 */
    BUCK2_disable(); /* buck2 */

    /* Disable the transient pulses. If it is running. */
    Cy_TCPWM_TriggerStopOrKill_Single(PWM_LOAD_HW, PWM_LOAD_NUM);

    /* Turn on Fault LED. */
    Cy_GPIO_Clr(FAULT_LED_PORT, FAULT_LED_NUM);

    /* Stop Run LED */
    Cy_TCPWM_PWM_SetCompare0Val(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, CLR_LED);

    /* Enables button IRQ after fault event*/
    NVIC_EnableIRQ(button_press_intr_config.intrSrc);

    /* Reset buck converter state to idle. */
    buck_state = Ifx_BUCK_STATE_FAULT;
}

/*******************************************************************************
* Function Name: buck1_pre_process_callback
*********************************************************************************
* Summary:
* This is the callback from the buck1 ISR running the control loop. In this
* function, protection logic for buck1 is implemented.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
__STATIC_INLINE void buck1_pre_process_callback(void) /* buck1 */
{
    /* Read result from ADC result register. */
    buck1_vout_adc_res = BUCK1_Vout_get_result();
    vin_adc_res        = BUCK1_Vin_get_result();
    buck1_iout_adc_res = BUCK1_Iout_get_result();
    buck1_temp_adc_res = BUCK1_Temp_get_result();

    /*Moving Average calculation*/
    buck1_vout_avg = (float32_t)((buck1_vout_avg - ((buck1_vout_avg - buck1_vout_adc_res) / AVERAGING_SAMPLES)));
    buck1_iout_avg = (float32_t)((buck1_iout_avg - ((buck1_iout_avg - buck1_iout_adc_res) / AVERAGING_SAMPLES)));
    buck1_temp_avg = (float32_t)((buck1_temp_avg - ((buck1_temp_avg - buck1_temp_adc_res) / AVERAGING_SAMPLES)));
    vin_avg        = (float32_t)((vin_avg        - ((vin_avg        - vin_adc_res)        / AVERAGING_SAMPLES)));

    /* Check for vin voltage, output current and temperature range */
    if((vin_avg < BUCK1_Vin_MIN) || (vin_avg > BUCK1_Vin_MAX) ||
       (buck1_iout_avg > BUCK1_Iout_MAX) ||
       (buck1_temp_avg > BUCK1_Temp_MAX))
    {
        /*Fault processing after detection of the fault*/
        fault_processing();
    }

    /* Check if output voltage protection is turned ON */
    if (buck1_enable_protection == true)
    {
        /* Check for under & over boundaries */
        if((buck1_vout_avg < BUCK1_Vout_MIN) || (buck1_vout_avg > BUCK1_Vout_MAX))
        {
            /*Fault processing after detection of the fault*/
            fault_processing();
        }
    }
    /* Check if output voltage protection can be turned ON */
    else if (buck1_vout_avg > BUCK1.ctx->targ)
    {
        buck1_enable_protection = true;
    }
}

/*******************************************************************************
* Function Name: buck2_pre_process_callback
*********************************************************************************
* Summary:
* This is the callback from the buck2 ISR running the control loop. In this
* function, protection logic for buck2 is implemented.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
__STATIC_INLINE void buck2_pre_process_callback(void) /* buck2 */
{
    /* Read result from ADC result register. */
    buck2_vout_adc_res = BUCK2_Vout_get_result();
    buck2_iout_adc_res = BUCK2_Iout_get_result();
    buck2_temp_adc_res = BUCK2_Temp_get_result();

    /*Moving Average calculation*/
    buck2_vout_avg = (float32_t)((buck2_vout_avg - ((buck2_vout_avg - buck2_vout_adc_res) / AVERAGING_SAMPLES)));
    buck2_iout_avg = (float32_t)((buck2_iout_avg - ((buck2_iout_avg - buck2_iout_adc_res) / AVERAGING_SAMPLES)));
    buck2_temp_avg = (float32_t)((buck2_temp_avg - ((buck2_temp_avg - buck2_temp_adc_res) / AVERAGING_SAMPLES)));

    /* Check for output current and temperature range */
    if((buck2_iout_avg > BUCK2_Iout_MAX) ||
       (buck2_temp_avg > BUCK2_Temp_MAX))
    {
        /*Fault processing after detection of the fault*/
        fault_processing();
    }

    /* Check if output voltage protection is turned ON */
    if (buck2_enable_protection == true)
    {
        /* Check for under & over boundaries */
        if((buck2_vout_avg < BUCK2_Vout_MIN) || (buck2_vout_avg > BUCK2_Vout_MAX))
        {
            /*Fault processing after detection of the fault*/
            fault_processing();
        }
    }
    /* Check if output voltage protection can be turned ON */
    else if (buck2_vout_avg > BUCK2.ctx->targ)
    {
        buck2_enable_protection = true;
    }
}

#endif  /* BUCK_PROTECTION_H */
/* [] END OF FILE */
