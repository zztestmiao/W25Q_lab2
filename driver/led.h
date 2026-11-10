#ifndef __LED_H__
#define __LED_H__

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"

/* 定义描述led的结构体 */
struct led_desc
{
	GPIO_TypeDef* Port; //端口
	uint32_t Pin;		//引脚
	BitAction OnBit;	//亮灯
	BitAction OffBit;	//灭灯
};

typedef struct led_desc* led_desc_t;

/* 延迟函数 */
void cpu_delay(void);

/* 3个led初始化的函数 */
void led_init(led_desc_t led);

/* 操作灯亮灭的函数 */
void led_set(led_desc_t led, bool onoff);

/* 指定灯亮的函数 */
void led_on(led_desc_t led);

/* 指定灯灭的函数 */
void led_off(led_desc_t led);

/* 灯全灭的函数 */
//void led_all_off(void);

#endif /* __LED_H__ */
