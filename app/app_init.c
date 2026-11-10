#include "app_init.h"
#include "board.h"
#include "led.h"
#include "w25q.h"

/* 启动时首先调用，准备板级资源、LED和SPI。
 * 不发送Flash修改命令，不启动TIM6。 */
void app_hardware_init(void)
{
    board_lowlevel_init();
    led_init(&led1);
    led_init(&led2);
    led_init(&led3);
    led_off(&led1);
    led_off(&led2);
    led_off(&led3);
	
    w25q_init();
}

