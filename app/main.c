#include "stm32f4xx.h"
#include "board.h"
#include "led.h"
#include "misc.h"
#include "w25q.h"

/* 两颗Flash从地址1开始的16字节快照，供Watch展开观察。
 * 数组放在STM32 RAM；这里只读取原内容，不修改外部Flash。
 * 读取状态：0未执行，1完成，2失败，3因ID不符合而跳过。 */
volatile uint8_t g_w25q1_data[16];
volatile uint8_t g_w25q2_data[16];
volatile uint32_t g_w25q1_read_status;
volatile uint32_t g_w25q2_read_status;

/* 用于调试器Watch观察，volatile确保结果在内存中可见。
 * 0表示尚未完成，1表示收发成功，2表示通信超时。
 * 收发成功不等于型号正确，还需要核对实际ID。 */
volatile uint32_t g_w25q1_id = 0u;
volatile uint32_t g_w25q2_id = 0u;
volatile uint32_t g_w25q1_status = 0u;
volatile uint32_t g_w25q2_status = 0u;

/* SR1是芯片返回的状态字节，sr1_result是通信结果。
 * result：0未执行、1完成、2失败、3因ID不符合跳过。 */
volatile uint8_t g_w25q1_sr1;
volatile uint8_t g_w25q2_sr1;
volatile uint32_t g_w25q1_sr1_result;
volatile uint32_t g_w25q2_sr1_result;

/* 第一颗Flash的WEL实验快照。结果0未执行、1通过、2失败、3跳过。
 * after_disable_result为1表示已通信确认WEL清零，否则不能认为清理成功。 */
volatile uint8_t g_wel_before;
volatile uint8_t g_wel_after_enable;
volatile uint8_t g_wel_after_disable;
volatile uint32_t g_wel_test_result;
volatile uint32_t g_wel_disable_result;

static void app_timer_init(void)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef interrupt;
    uint32_t timer_clock;

    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    /* F407：APB1分频不为1时，定时器时钟是APB1时钟的两倍。 */
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
    {
        timer_clock *= 2u;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
    TIM_TimeBaseStructInit(&timer);
	
    /* 先分频到每秒10000次计数，再每5000次产生更新事件，约0.5秒。 */
    timer.TIM_Prescaler = (uint16_t)(timer_clock / 10000u - 1u);
    timer.TIM_Period = 5000u - 1u;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &timer);

    /* 初始化可能产生更新标志，开启中断前先清除。 */
    TIM_ClearITPendingBit(TIM6, TIM_IT_Update);//清除 TIM6 外设内部的更新中断标志
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);//清除 NVIC 中该通道等待处理的请求
	
    interrupt.NVIC_IRQChannel = TIM6_DAC_IRQn;
    interrupt.NVIC_IRQChannelPreemptionPriority = 1;
    interrupt.NVIC_IRQChannelSubPriority = 0;
    interrupt.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&interrupt);
	
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
}

/* 名字必须与启动文件中的向量表项一致，由硬件中断进入。 */
void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
        GPIO_ToggleBits(led1.Port, led1.Pin);
    }
}

int main(void)
{
    uint32_t id;
    bool success;
    uint8_t snapshot[16] = {0};
    uint8_t status1 = 0u;

    board_lowlevel_init();
    led_init(&led1);
    led_init(&led2);
    led_init(&led3);
    led_off(&led1);
    led_off(&led2);
    led_off(&led3);
	
    w25q_init();
	
    success = w25q_read_jedec_id(1u, &id);
    g_w25q1_id = id;
    g_w25q1_status = success ? 1u : 2u;
	
    success = w25q_read_jedec_id(2u, &id);
    g_w25q2_id = id;
    g_w25q2_status = success ? 1u : 2u;
	
    /* 先确认型号，再按16MiB芯片的地址范围进行读取。 */
	//#2.读取芯片1的数据
    if (g_w25q1_status == 1u && g_w25q1_id == 0xEF4018u)
    {
		//这里读取了16个字节，逐个写入snapshot
        /* 起点改为1：前15字节应对应上次地址0快照的第1～15项。
         * 新的最后一字节来自地址16，其内容需要实测。 */
        success = w25q_read_data(1u, 1u, snapshot, sizeof(snapshot));
        for (uint32_t i = 0; i < sizeof(snapshot); ++i)
        {
            g_w25q1_data[i] = snapshot[i];
        }
        g_w25q1_read_status = success ? 1u : 2u;
    }
    else
    {
        g_w25q1_read_status = 3u;
    }
	
	//#3.读取芯片2的数据
    if (g_w25q2_status == 1u && g_w25q2_id == 0xEF4018u)
    {
        /* 清空快照，避免失败后混入第一颗芯片的旧结果。 */
        for (uint32_t i = 0; i < sizeof(snapshot); ++i)
        {
            snapshot[i] = 0u;
        }
        success = w25q_read_data(2u, 1u, snapshot, sizeof(snapshot));
        for (uint32_t i = 0; i < sizeof(snapshot); ++i)
        {
            g_w25q2_data[i] = snapshot[i];
        }
        g_w25q2_read_status = success ? 1u : 2u;
    }
    else
    {
        g_w25q2_read_status = 3u;
    }
	
    /* 仅在对应型号识别成功后读取状态，成功时才保存结果。 */
    if (g_w25q1_status == 1u && g_w25q1_id == 0xEF4018u)
    {
        success = w25q_read_status1(1u, &status1);
        if (success)
        {
            g_w25q1_sr1 = status1;
        }
        g_w25q1_sr1_result = success ? 1u : 2u;
    }
    else
    {
        g_w25q1_sr1_result = 3u;
    }
    if (g_w25q2_status == 1u && g_w25q2_id == 0xEF4018u)
    {
        success = w25q_read_status1(2u, &status1);
        if (success)
        {
            g_w25q2_sr1 = status1;
        }
        g_w25q2_sr1_result = success ? 1u : 2u;
    }
    else
    {
        g_w25q2_sr1_result = 3u;
    }

    /* 先仅在第一颗芯片上验证WEL，第二颗保持原来的只读流程。
     * 使用前面刚读到的SR1，只有通信成功、型号正确且BUSY为0才开始。 */
    if (g_w25q1_status == 1u && g_w25q1_id == 0xEF4018u &&
        g_w25q1_sr1_result == 1u && (g_w25q1_sr1 & 0x01u) == 0u)
    {
        g_wel_before = g_w25q1_sr1;
        success = w25q_write_enable(1u);
        if (success)
        {
            success = w25q_read_status1(1u, &status1);
            if (success)
            {
                g_wel_after_enable = status1;
                success = (status1 & 0x03u) == 0x02u;
            }
        }
        g_wel_test_result = success ? 1u : 2u;

        /* 即使使能或校验失败，也尝试04关闭写使能。
         * 如果清理失败，结果为2，不能仅凭快照零值认为已关闭。 */
        success = w25q_write_disable(1u);
        if (success)
        {
            success = w25q_read_status1(1u, &status1);
            if (success)
            {
                g_wel_after_disable = status1;
                success = (status1 & 0x03u) == 0u;
            }
        }
        g_wel_disable_result = success ? 1u : 2u;
    }
    else
    {
        g_wel_test_result = 3u;
        g_wel_disable_result = 3u;
    }

    app_timer_init();

    /* 主循环不再控制灯；LED1亮灭只能由TIM6中断触发。 */
    while (1)
    __NOP();
}
