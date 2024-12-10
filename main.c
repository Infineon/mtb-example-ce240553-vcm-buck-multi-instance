/*******************************************************************************
* File Name:   main.c
*
* Description: Multi instance synchronous voltage control mode buck regulator
* implementation using PCC tool and power conversion middleware for the PSOC(TM)
* Control C3M5 Compete System Dual Buck Evaluation Kit. This is the main file
* for the regulator.
*
* Related document: See README.md
*
*
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
* Header files
*******************************************************************************/
#include "cy_pdl.h"
#include "cy_retarget_io.h"
#include "mtb_hal.h"
#include "buck_protection.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* Macros defined to calculate voltage and current. */
#define MAX_ADC_COUNT     (4095)           /* ADC maximum count. */
#define OUTPUT_CRNT_DVDR  (0.5)            /* Output current divider. */
#define OUTPUT_VOLT_DVDR  (0.239)          /* Output voltage divider. */
#define REF_VALUE_ADC     (3.3)            /* ADC reference value. */

/*******************************************************************************
* Global variables
*******************************************************************************/

/* Interrupt configuration structure of soft start */
cy_stc_sysint_t soft_start_irq_cfg =
{
    .intrSrc = SOFT_START_COUNTER_IRQ,
    .intrPriority = 2UL,
};

/* Interrupt configuration structure of button GPIO. */
cy_stc_sysint_t button_press_intr_config =
{
    .intrSrc = USER_BUTTON_IRQ,
    .intrPriority = 2UL,
};

/* Variables for protection for converter 1. */ /* buck1 */
float32_t buck1_vout_adc_res          = 0;
float32_t buck1_iout_adc_res          = 0;
float32_t buck1_temp_adc_res          = 0;
float32_t vin_adc_res                 = 0;
float32_t buck1_vout_avg              = 0;
float32_t buck1_iout_avg              = 0;
float32_t buck1_temp_avg              = 0;
float32_t vin_avg                     = VIN_COUNT;
uint32_t buck1_enable_protection      = false;

/* Variables for protection for converter 2. */ /* buck2 */
float32_t buck2_vout_adc_res          = 0;
float32_t buck2_iout_adc_res          = 0;
float32_t buck2_temp_adc_res          = 0;
float32_t buck2_vout_avg              = 0;
float32_t buck2_iout_avg              = 0;
float32_t buck2_temp_avg              = 0;
uint32_t buck2_enable_protection      = false;

/* State variable. */
Ifx_buck_states buck_state = Ifx_BUCK_STATE_IDLE;

/* Debug UART variables. */
static cy_stc_scb_uart_context_t    DEBUG_UART_context; /* UART context. */
static mtb_hal_uart_t               DEBUG_UART_hal_obj; /* Debug UART HAL object. */

/* Variables for calculating voltage and current for debug prints. */
const float32_t volt_multiplier    = ((REF_VALUE_ADC)/MAX_ADC_COUNT/OUTPUT_VOLT_DVDR);
const float32_t current_multiplier = ((REF_VALUE_ADC)/MAX_ADC_COUNT/OUTPUT_CRNT_DVDR);
/*******************************************************************************
* Function prototypes
*******************************************************************************/
/* Interrupt handler for soft start. */
void soft_start_intr_handler(void);

/* Interrupt handler for button interrupt. */
void button_press_intr_handler(void);

/* Function for peripheral initialization and enabling. */
void hardware_init(void);

/*******************************************************************************
* Function definitions
*******************************************************************************/

/*******************************************************************************
* Function name: hardware_init
*********************************************************************************
* Summary:
* This is the hardware peripheral initialization and enabling function.
* In this function all peripheral including TCPWM, HPPASS, and interrupt are configured.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void hardware_init(void)
{
    cy_rslt_t result;

    /* Initializes the timer for transient load testing. */
    result = Cy_TCPWM_PWM_Init(PWM_LOAD_HW, PWM_LOAD_NUM, &PWM_LOAD_config);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initializes the timer for soft starting. */
    result = Cy_TCPWM_Counter_Init(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM, &SOFT_START_COUNTER_config);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initializes the timer used for status LED. */
    result = Cy_TCPWM_PWM_Init(PWM_STATUS_LED_HW, PWM_STATUS_LED_NUM, &PWM_STATUS_LED_config);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initializes the timer used for ACT LED. */
    result = Cy_TCPWM_PWM_Init(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, &PWM_ACT_LED_config);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initializes the interrupt for a soft start. */
    result = Cy_SysInt_Init(&soft_start_irq_cfg, soft_start_intr_handler);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /*Initializes the interrupt for button action. */
    result = Cy_SysInt_Init(&button_press_intr_config, button_press_intr_handler);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Clears all pending interrupts before configuring interrupts. */
    NVIC_ClearPendingIRQ(soft_start_irq_cfg.intrSrc);
    NVIC_ClearPendingIRQ(button_press_intr_config.intrSrc);

    /* Enables all interrupts. */
    NVIC_EnableIRQ(soft_start_irq_cfg.intrSrc);
    NVIC_EnableIRQ(button_press_intr_config.intrSrc);

    /* Enables all timer channels. */
    Cy_TCPWM_PWM_Enable(PWM_LOAD_HW, PWM_LOAD_NUM);
    Cy_TCPWM_Counter_Enable(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM);
    Cy_TCPWM_PWM_Enable(PWM_STATUS_LED_HW, PWM_STATUS_LED_NUM);
    Cy_TCPWM_PWM_Enable(PWM_ACT_LED_HW, PWM_ACT_LED_NUM);
}

/*******************************************************************************
* Function name: soft_start_intr_handler
*********************************************************************************
* Summary:
* This is the interrupt service routine (ISR) for the soft start counter interrupt.
* It will be executed only until the completion of the soft start. It gradually
* increases the reference values until reaching the calculated final reference
* values in both converters.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void soft_start_intr_handler(void)
{
    /* Clears soft start interrupt. */
    Cy_TCPWM_ClearInterrupt(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM, CY_TCPWM_INT_ON_TC);

    /* Increments the reference value for buck1 and buck2. */
    BUCK1_ramp(); /* buck1 */
    BUCK2_ramp(); /* buck2 */

    /* Stops the counter when both counters are reached the reference value. */
    if((BUCK1_ctx.res >= BUCK1_ctx.targ) &&  /* buck1 */
       (BUCK2_ctx.res >= BUCK2_ctx.targ))    /* buck2 */
    {
        /* Disables soft start IRQ after the completion of soft start. */
        Cy_TCPWM_TriggerStopOrKill_Single(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM);

        /* Enables button IRQ after soft start. */
        NVIC_EnableIRQ(button_press_intr_config.intrSrc);
    }
}

/*******************************************************************************
* Function name: button_press_intr_handler
*********************************************************************************
* Summary:
* This is the button interrupt handler that control the converter state machine.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void button_press_intr_handler(void)
{
    /* Clears the GPIO interrupt. */
    Cy_GPIO_ClearInterrupt(USER_BUTTON_PORT, USER_BUTTON_NUM);

    switch (buck_state)
    {
        case Ifx_BUCK_STATE_IDLE: /* Button press when the state is Idle. State will switch to run state. */
        {
            /* Resets variables used for protection of converter 1. */ /* buck1 */
            buck1_enable_protection = false;
            buck1_vout_avg     = 0;
            buck1_iout_avg     = 0;
            buck1_temp_avg     = 0;
            vin_avg            = VIN_COUNT;
            buck1_vout_adc_res = 0;
            buck1_iout_adc_res = 0;

            /* Resets variables used for protection of converter 2. */ /* buck2 */
            buck2_enable_protection = false;
            buck2_vout_avg     = 0;
            buck2_iout_avg     = 0;
            buck2_temp_avg     = 0;
            buck2_vout_adc_res = 0;
            buck2_iout_adc_res = 0;

            /* Enables the buck converters. */
            BUCK1_enable(); /* buck1 */
            BUCK2_enable(); /* buck2 */

            /* Starts the buck converters simultaneously. */
            pwrconv_start();

            /* Disables button IRQ to avoid button actions during soft start. */
            NVIC_DisableIRQ(button_press_intr_config.intrSrc);

            /* Starts the soft start of the converter. */
            Cy_TCPWM_TriggerStart_Single(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM);

            /* Setting the LED indicating the run status. */
            Cy_TCPWM_PWM_SetCompare0Val(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, SET_LED);

            /* Turns OFF fault LED. */
            Cy_GPIO_Set(FAULT_LED_PORT, FAULT_LED_NUM);

            /* Updates the state of the converter. */
            buck_state = Ifx_BUCK_STATE_RUN;

            break;
        }

        case Ifx_BUCK_STATE_RUN: /* Button press when state is Run. State will switch to Test state. */
        {
            /* Sets the counter for blinking the ACT LED. */
            Cy_TCPWM_TriggerStart_Single(PWM_LOAD_HW, PWM_LOAD_NUM);

            /* Starts PWMs for transient testing. */
            Cy_TCPWM_PWM_SetCompare0Val(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, TOGGLE_LED);

            /* Updates the state of the converter. */
            buck_state = Ifx_BUCK_STATE_TEST;

            break;
        }

        case Ifx_BUCK_STATE_TEST: /* Button press when state is Test. State will switch to Idle state. */
        {
            /* Stops run LED. */;
            Cy_TCPWM_PWM_SetCompare0Val(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, CLR_LED);

            /* Stops PWMs for transient testing. */
            Cy_TCPWM_TriggerStopOrKill_Single(PWM_LOAD_HW, PWM_LOAD_NUM);

            /* Stops the buck converter. */
            BUCK1_disable(); /* buck1 */
            BUCK2_disable(); /* buck2 */

            /* Stops the soft start timer. */
            Cy_TCPWM_TriggerStopOrKill_Single(SOFT_START_COUNTER_HW, SOFT_START_COUNTER_NUM);

            /* Updates the state of the converter. */
            buck_state = Ifx_BUCK_STATE_IDLE;

            break;
        }

        case Ifx_BUCK_STATE_FAULT: /* Button press when state is Fault. State will switch to Idle state. */
        {
            /* Turns OFF fault LED. */
            Cy_GPIO_Set(FAULT_LED_PORT, FAULT_LED_NUM);

            /* Updates the state of the converter. */
            buck_state = Ifx_BUCK_STATE_IDLE;

            break;
        }

        default:
        {
            /* Updates the state of the converter. */
            buck_state = Ifx_BUCK_STATE_IDLE;

            break;
        }
    }
}

/*******************************************************************************
* Function name: main
********************************************************************************
* Summary:
* This is the main function of the code example. It performs the initialization
* and printing of the message.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initializes the device and board peripherals. */
    result = cybsp_init();

    /* Board init failed. Stop program execution. */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Configures retarget-io to use the debug UART port. */
    result = (cy_rslt_t)Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config,
                                         &DEBUG_UART_context);

    /* UART init failed. Stops program execution. */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup the HAL UART. */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config,
                                &DEBUG_UART_context, NULL);

    /* HAL UART init failed. Stop program execution. */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initializes redirecting of low-level I/O. */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    /* Retarget I/O init failed. Stops program execution. */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enables global interrupts. */
    __enable_irq();

    /* Initializes the peripherals. */
    hardware_init();

    /* Initializes LEDs. */
    Cy_GPIO_Set(FAULT_LED_PORT, FAULT_LED_NUM);  /* Protection condition violation. */

    /* Starts the TCPWM for LEDs. */
    Cy_TCPWM_TriggerStart_Single(PWM_STATUS_LED_HW, PWM_STATUS_LED_NUM);  /* Code is running. */
    Cy_TCPWM_PWM_SetCompare0Val(PWM_ACT_LED_HW, PWM_ACT_LED_NUM, CLR_LED);
    Cy_TCPWM_TriggerStart_Single(PWM_ACT_LED_HW, PWM_ACT_LED_NUM);        /* Converter running/transient testing. */

    /* Prints the start of the converter information to the terminal. */
    printf("\x1b[2J\x1b[;H");
    printf("\r\n---------------------------------------------------------------------------------------------------------------------------------------------------"
           "\r\nThis code example demonstrates the voltage control mode multi-instance buck converter implementation on the KIT_PSC3M5_DP1."
           "\r\n "
           "\r\nPress events on the user button (USER_BTN) on the dual buck evaluation board takes the converter through the following states."
           "\r\n1. Converter ON - Converter will regulate the output voltage to the 5 V target. ACT_LED(D5) on the dual buck evaluation board will glow."
           "\r\n2. Transient ON - Activates load transient pulses to evaluate regulation performance on the output target voltage. ACT_LED will toggle. "
           "\r\n3. Converter OFF - Stops the output voltage regulation. ACT_LED will be off."
           "\r\n"
           "\r\nThe STATUS LED on the control card will blink always. FAULT LED on the dual buck evaluation board will glow when the converter detected a fault."
           "\r\n"
           "\r\nKIT_PSC3M5_DP1 comes with variable load and transient load."
           "\r\nTo test the converters by using variable load, keep the SPDT switches SW4 and SW5 in variable mode,"
           "\r\nturn ON the converter by pressing the user button, and rotate the potentiometers R42 and R61 to vary the load current."
           "\r\nTo test using the transient load, keep the SPDT switches in the transient mode and switch the converter to the transient test mode. "
           "\r\n"
           "\r\nBefore turning on the output, ensure that the 24 V wall adapter is connected to the board, and the header (J14) is not shorted."
           "\r\n"
           "\r\nFor more information, see the README.md of the mtb-example-ce240553-vcm-buck-multi-instance code example"
           "\r\n---------------------------------------------------------------------------------------------------------------------------------------------------\r\n"
           "\r\nThe converter state, output voltage, and load current are as follows:\r\n");

    for (;;)
    {
        /*Printing the active state with output volatge and load*/
        switch (buck_state)
        {
        case Ifx_BUCK_STATE_IDLE:
        {
            printf("\rRegulation Off Transient pulse Off                                                                ");
            break;
        }
        case Ifx_BUCK_STATE_RUN:
        {
            printf("\rRegulation On Transient pulse Off BUCK1_VOUT=%.2f V  LOAD1=%.2f A  BUCK2_VOUT=%.2f V  LOAD2=%.2f A  ",((float64_t)buck1_vout_adc_res*volt_multiplier)
                                                                                                                           ,((float64_t)buck1_iout_adc_res*current_multiplier)
                                                                                                                           ,((float64_t)buck2_vout_adc_res*volt_multiplier)
                                                                                                                           ,((float64_t)buck2_iout_adc_res*current_multiplier));
            break;
        }
        case Ifx_BUCK_STATE_TEST:
        {
            printf("\rRegulation On Transient pulse On BUCK1_VOUT=%.2f V  LOAD1=%.2f A  BUCK2_VOUT=%.2f V  LOAD2=%.2f A  ",((float64_t)buck1_vout_adc_res*volt_multiplier)
                                                                                                                          ,((float64_t)buck1_iout_adc_res*current_multiplier)
                                                                                                                          ,((float64_t)buck2_vout_adc_res*volt_multiplier)
                                                                                                                          ,((float64_t)buck2_iout_adc_res*current_multiplier));
            break;
        }
        case Ifx_BUCK_STATE_FAULT:
        {
            printf("\rFault                                                                                            ");
            break;
        }
        default:
        {
            break;
        }
        }
    }
}

/* [] END OF FILE */
