#ifndef APP_TIMER_H
#define APP_TIMER_H

/* 硬件及LED初始化、启动阶段Flash实验结束后调用一次。
 * 配置并开启TIM6更新中断，每约0.5秒翻转LED1。
 * 本模块独占TIM6及TIM6_DAC中断，其他实验不得重复定义处理函数。
 * 当前未启用DAC中断源，未来使用DAC须统一扩展此共享中断处理。
 * 本函数不是通用计时服务，不提供停止、重复启动或并发调用保证。 */
void app_timer_start(void);

#endif
