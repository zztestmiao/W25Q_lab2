#include "flash_test.h"
#include "w25q.h"

/* 本文件集中管理启动时的Flash实验和Watch快照。
 * 只依赖W25Q驱动，不负责LED、定时器或板级初始化。
 * Watch变量只在此定义，不通过头文件公开给其他模块。 */
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
 * g_wel_disable_result为1表示已通信确认WEL清零，否则不能认为清理成功。 */
volatile uint8_t g_wel_before;
volatile uint8_t g_wel_after_enable;
volatile uint8_t g_wel_after_disable;
volatile uint32_t g_wel_test_result;
volatile uint32_t g_wel_disable_result;

/* 在硬件初始化后调用，识别两颗Flash并保存ID及通信结果。
 * 后续实验依据这些结果决定是否执行。 */
void flash_test_identify(void)
{
    uint32_t id;
    bool success;

    success = w25q_read_jedec_id(1u, &id);
    g_w25q1_id = id;
    g_w25q1_status = success ? 1u : 2u;
	
    success = w25q_read_jedec_id(2u, &id);
    g_w25q2_id = id;
    g_w25q2_status = success ? 1u : 2u;
}

/* 在识别后调用，从两颗Flash的地址1各读16字节。
 * snapshot是本函数局部缓冲区，返回前复制到全局Watch数组。
 * 读取失败时数组可能仅部分有效，必须同时检查read_status。 */
void flash_test_read_data(void)
{
    bool success;
    uint8_t snapshot[16] = {0};

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
}

/* 在识别后调用，保存两颗Flash的SR1快照，不修改芯片内容。
 * 仅通信成功才覆盖状态字节，失败或跳过见sr1_result。 */
void flash_test_read_status(void)
{
    bool success;
    uint8_t status1 = 0u;

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
}

/* 必须在识别和状态读取之后、启动TIM6之前调用。
 * 第一颗执行06→05→04→05，记录快照及两项验证结果。
 * 使能失败也尝试关闭；不擦除、不编程，清理失败单独报告。
 * 当前仅在启动时调用一次，不应并发或反复调用。 */
void flash_test_wel(void)
{
    bool success;
    uint8_t status1 = 0u;

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
}


/* 完整候选扇区放在全局RAM，避免4096字节占用函数栈。
 * buffer仅为RAM快照，断电丢失，尚不等于电脑上的持久备份。
 * result：0未执行、1完整读取成功、2失败、3前置条件不满足。
 * non_ff仅在result为1时有效；全FF也不能证明该区域无人使用。 */
uint8_t g_sector_snapshot[4096];
volatile uint32_t g_sector_read_result;
volatile uint32_t g_sector_non_ff;

/* 在识别、状态及WEL实验后、启动TIM6前单次调用。
 * 只检查第一颗最后一个4KiB扇区，直接读取到全局缓冲区。
 * 失败可能留下部分内容，此时禁止用它作为完整备份或擦除依据。 */
void flash_test_sector_read(void)
{
    uint8_t status1;
    uint32_t i;
    uint32_t non_ff = 0u;

    if (g_w25q1_status != 1u || g_w25q1_id != 0xEF4018u)
    {
        g_sector_read_result = 3u;
        return;
    }
    /* 重新查询当前状态，不用之前的旧快照判断空闲。 */
    if (!w25q_read_status1(1u, &status1))
    {
        g_sector_read_result = 2u;
        return;
    }
    if ((status1 & 0x01u) != 0u)
    {
        g_sector_read_result = 3u;
        return;
    }
    if (!w25q_read_data(1u, 0xFFF000u,
                        g_sector_snapshot, sizeof(g_sector_snapshot)))
    {
        g_sector_read_result = 2u;
        return;
    }
    for (i = 0u; i < sizeof(g_sector_snapshot); ++i)
    {
        if (g_sector_snapshot[i] != 0xFFu)
        {
            ++non_ff;
        }
    }
    g_sector_non_ff = non_ff;
    /* 最后发布成功，供调试器确认整个数组及统计已经准备好。 */
    g_sector_read_result = 1u;
}
