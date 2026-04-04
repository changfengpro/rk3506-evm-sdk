/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include "hal_bsp.h"
#include "hal_base.h"

/********************* Private MACRO Definition ******************************/
// #define TEST_DEMO
// #define DEBUG_FLAG_ADDR  0x03b00000
// #define WRITE_FLAG(val)  (*((volatile uint32_t *)DEBUG_FLAG_ADDR) = (val))
/********************* Private Structure Definition **************************/

/********************* Private Variable Definition ***************************/
static uint32_t count;
/********************* Private Function Definition ***************************/

/********************* Public Function Definition ****************************/
static struct UART_REG *pUart = UART3;

#ifdef __GNUC__
__USED int _write(int fd, char *ptr, int len)
{
    int i = 0;

    /*
     * write "len" of char from "ptr" to file id "fd"
     * Return number of char written.
     *
    * Only work for STDOUT, STDIN, and STDERR
     */
    if (fd > 2) {
        return -1;
    }

    while (*ptr && (i < len)) {
        if (*ptr == '\n') {
            HAL_UART_SerialOutChar(pUart, '\r');
        }
        HAL_UART_SerialOutChar(pUart, *ptr);

        i++;
        ptr++;
    }

    return i;
}
#else
int fputc(int ch, FILE *f)
{
    if (ch == '\n') {
        HAL_UART_SerialOutChar(pUart, '\r');
    }

    HAL_UART_SerialOutChar(pUart, (char)ch);

    return 0;
}
#endif

int main(void)
{
    struct HAL_UART_CONFIG hal_uart_config = {
        .baudRate = UART_BR_115200,
        .dataBit = UART_DATA_8B,
        .stopBit = UART_ONE_STOPBIT,
        .parity = UART_PARITY_DISABLE,
    };
    /* HAL BASE Init */
    HAL_Init();

    /* BSP Init */
    BSP_Init();

    /* INTMUX Init */
    HAL_INTMUX_Init();

    /* UART Init */

    HAL_CRU_ClkEnable(PCLK_UART3_GATE);
    HAL_CRU_ClkEnable(SCLK_UART3_SRC_GATE);

    HAL_PINCTRL_SetIOMUX(GPIO_BANK0, GPIO_PIN_A0, PIN_CONFIG_MUX_FUNC4); 
    HAL_PINCTRL_SetIOMUX(GPIO_BANK0, GPIO_PIN_A1, PIN_CONFIG_MUX_FUNC4);

    // HAL_PINCTRL_SetRMIO(GPIO_BANK0,
    //                     GPIO_PIN_A0,
    //                     RMIO_UART3_TX);
    // HAL_PINCTRL_SetRMIO(GPIO_BANK0,
    //                     GPIO_PIN_A1,
    //                     RMIO_UART3_RX);

    HAL_UART_Init(&g_uart3Dev, &hal_uart_config);

    HAL_UART_SerialOutChar(pUart, '\n');

    HAL_UART_SerialOutChar(pUart, 'O');
    HAL_UART_SerialOutChar(pUart, 'K');
    HAL_UART_SerialOutChar(pUart, '\n');

    HAL_DBG("\n=======================================\n");
    HAL_DBG("Hello RK3506 MCU - UART3 is ALIVE!!!\n");
    HAL_DBG("=======================================\n");

#ifdef TEST_DEMO
    test_demo();
#endif

    while (1) {
    
        HAL_DBG("MCU is running, tick: %lu\n", count++);
        const char *msg = "MCU is running, tick: ";
        for (int i = 0; msg[i] != '\0'; i++) {
            HAL_UART_SerialOutChar(pUart, msg[i]);
        }

        HAL_DelayMs(1000);
    //       __asm volatile ("wfi");
    }
}

int entry(void)
{
    return main();
}
