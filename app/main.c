#include "stm32f4xx.h"
#include "app_init.h"
#include "app_timer.h"
#include "flash_test.h"

int main(void)
{
    /* 先准备接口，再按识别、读取、状态、写使能的顺序完成实验。 */
    app_hardware_init();
    flash_test_identify();
    flash_test_read_data();
    flash_test_read_status();
    flash_test_wel();

    /* 实验完成后启动TIM6，LED1每约0.5秒翻转。 */
    app_timer_start();

    /* 主循环等待中断，不重复执行Flash实验。 */
    while (1)
    {
        __NOP();
    }
}
