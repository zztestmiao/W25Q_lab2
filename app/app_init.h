#ifndef APP_INIT_H
#define APP_INIT_H

/* 程序启动时调用一次，先于实验和定时器。
 * 准备板级资源、三颗LED并关闭它们，再初始化W25Q通信接口。
 * 本函数配置硬件，不识别Flash，不擦写，也不启动TIM6。
 * 后续新外设确有初始化需求时在此组织，实验逻辑留在实验模块。 */
void app_hardware_init(void);

#endif
