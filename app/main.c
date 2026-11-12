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
    /* 只读候选测试扇区到RAM，供后续检查和导出备份。 */
    flash_test_sector_read();

    /* 实验完成后启动TIM6，LED1每约0.5秒翻转。 */
    app_timer_start();

    /* 主循环轮询手动擦除及编程请求，启动阶段的其他实验不重复执行。 */
    while (1)
    {
        flash_test_erase_poll();
        flash_test_program_poll();
        flash_test_cross_poll();
        flash_test_download_poll();
        __NOP();
    }
}
