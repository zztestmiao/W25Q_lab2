#ifndef __BOARD_H__
#define __BOARD_H__

#include "led.h"

extern struct led_desc led1;
extern struct led_desc led2;
extern struct led_desc led3;
	
/* 板上初始化函数 */
void board_lowlevel_init();

#endif /* __BOARD_H__ */