#include "stm32f4xx.h"
#include <stdlib.h>
#include <stdbool.h>	
#include "led.h"

//LED1:PB0
//LED2:PB1
//LED3:PE9

#define LED1_PORT GPIOB
#define LED1_PIN  GPIO_Pin_0

#define LED2_PORT GPIOB
#define LED2_PIN  GPIO_Pin_1

#define LED3_PORT GPIOE
#define LED3_PIN  GPIO_Pin_9

/* 延迟函数 */
void cpu_delay(void)
{	
	uint32_t delay = 6*1000*1000;
	uint32_t i = 0;
	for(i = 0; i < delay; i++){}
}

/* 3个led初始化的函数 */
void led_init(led_desc_t led)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	//可以直接给结构体全部赋初始值
	GPIO_StructInit(&GPIO_InitStructure);

	//给GPIOE的结构体赋值
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
	
	//根据结构体里的配置,直接写GPIO相关寄存器,完成引脚配置
	GPIO_InitStructure.GPIO_Pin = led->Pin;
	GPIO_Init(led->Port, &GPIO_InitStructure);//led1
}


/* 操作灯亮灭的函数 */
void led_set(led_desc_t led, bool onoff)
{
	if(led == NULL)
		return;
	GPIO_WriteBit(led->Port, led->Pin, onoff ? led->OnBit: led->OffBit);
}

/* 指定灯亮的函数 */
void led_on(led_desc_t led)
{
	GPIO_WriteBit(led->Port, led->Pin, led->OnBit);
}

/* 指定灯灭的函数 */
void led_off(led_desc_t led)
{
	GPIO_WriteBit(led->Port, led->Pin, led->OffBit);
}

/* 灯全灭的函数 */
//void led_all_off(void)
//{
//	led_off(1);
//    led_off(2);
//    led_off(3);
//}

