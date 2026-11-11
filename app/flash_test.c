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
    uint8_t status1;//保存读取的状态寄存器1
    uint32_t i;
    uint32_t non_ff = 0u;//统计非0xFF字节的数量，供调试器观察

    // 仅在第一颗芯片识别成功且型号正确时才尝试读取扇区
    if (g_w25q1_status != 1u || g_w25q1_id != 0xEF4018u)
    {
        g_sector_read_result = 3u;
        return;
    }
    /* 重新查询当前状态，不用之前的旧快照判断空闲。 */
    //查询状态寄存器1，保存到status1，如果失败则设置g_sector_read_result为2并返回
    if (!w25q_read_status1(1u, &status1))
    {
        g_sector_read_result = 2u;
        return;
    }
    //如果状态寄存器的BUSY位为1，说明Flash正在忙碌中，则设置g_sector_read_result为3并返回
    if ((status1 & 0x01u) != 0u)
    {
        g_sector_read_result = 3u;
        return;
    }
    //读取最后一个4KiB扇区的内容到g_sector_snapshot
    //如果读取失败，g_sector_snapshot可能只更新了部分内容，不能作为完整备份
    if (!w25q_read_data(1u, 0xFFF000u,
                        g_sector_snapshot, sizeof(g_sector_snapshot)))
    {
        g_sector_read_result = 2u;
        return;
    }
    //如果读取成功，统计非0xFF字节的数量，存入g_sector_non_ff
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

/* 默认0不执行。用户确认区域用途及电脑备份后，在Watch写1触发。
 * 每次上电最多尝试一次，失败不自动重试。仅限第一颗0xFFF000扇区。
 * result：0未触发、1流程及全FF检查通过、2驱动失败、
 * 3前置条件不满足、4读回失败、5读回存在非FF、6正在执行。
 * 当前扇区原本全FF，通过不代表已验证从非FF恢复为FF的能力。 */
volatile uint32_t g_sector_erase_request;
volatile uint32_t g_sector_erase_result;
volatile uint32_t g_sector_after_non_ff;

void flash_test_erase_poll(void)
{
    static bool attempted = false;
    uint32_t i;
    uint32_t non_ff = 0u;

    if (attempted || g_sector_erase_request != 1u)
    {
        return;
    }
    /* 在操作前锁定单次尝试并消费请求，防止主循环反复擦除。
     * 即便失败也保留锁定，重新实验需复位并重新确认。 */
    attempted = true;
    g_sector_erase_request = 0u;
    if (g_w25q1_status != 1u || g_w25q1_id != 0xEF4018u ||
        g_sector_read_result != 1u)
    {
        g_sector_erase_result = 3u;
        return;
    }
    g_sector_erase_result = 6u;
    /* 此轮只用有限查询次数，非精确毫秒期限。
     * 驱动失败可能已提交擦除，立即停止，不读数据、不重试。 */
    if (!w25q_erase_sector(1u, 0xFFF000u, 100000u))
    {
        g_sector_erase_result = 2u;
        return;
    }
    /* 复用RAM快照，原始内容已在电脑保存；此后数组代表擦除后内容。 */
    if (!w25q_read_data(1u, 0xFFF000u,
                        g_sector_snapshot, sizeof(g_sector_snapshot)))
    {
        g_sector_erase_result = 4u;
        return;
    }
    for (i = 0u; i < sizeof(g_sector_snapshot); ++i)
    {
        if (g_sector_snapshot[i] != 0xFFu)
        {
            ++non_ff;
        }
    }
    g_sector_after_non_ff = non_ff;
    g_sector_erase_result = (non_ff == 0u) ? 1u : 5u;
}

/* 每次上电最多一次，request写1后尝试写16字节00～0F。
 * result：0未执行、1写入并比对通过、2前置不满足、3预读失败、
 * 4目标非全FF拒绝写入、5编程失败、6读回失败、7不一致、8执行中。
 * readback仅在结果1或7时有效，mismatch也仅在这两个结果下有效。 */
volatile uint32_t g_page_program_request;
volatile uint32_t g_page_program_result;
volatile uint32_t g_page_mismatch;
uint8_t g_page_readback[16];

void flash_test_program_poll(void)
{
    static bool attempted = false;
    uint8_t expected[16];
    uint8_t status1;
    uint32_t i;
    uint32_t mismatch = 0u;

    // 仅在第一次上电且用户确认后尝试编程，避免反复写入。
    if (attempted || g_page_program_request != 1u)
    {
        return;
    }
    attempted = true;
    // 消费请求，防止主循环反复编程。即便失败也保留锁定，重新实验需复位并重新确认。
    g_page_program_request = 0u;
    /* 操作时不要同时请求擦除与写入；main先轮询擦除，再轮询写入。
     * 若此前擦除失败则停止，不能忽略其未知状态继续写。 */
    if (g_w25q1_status != 1u || g_w25q1_id != 0xEF4018u ||
        g_sector_read_result != 1u ||
        (g_sector_erase_result != 0u && g_sector_erase_result != 1u))
    {
        g_page_program_result = 2u;
        return;
    }
    /* 先读回扇区末尾16字节，确认全FF后才尝试编程。
     * 读取失败或非全FF都不尝试写入，避免破坏原有数据。 */
    g_page_program_result = 8u;
    /* 读取前确认空闲，避免正在擦除或编程时发起新命令。 */
    if (!w25q_read_status1(1u, &status1) || (status1 & 0x01u) != 0u ||
        !w25q_read_data(1u, 0xFFF000u, g_page_readback, sizeof(g_page_readback)))
    {
        g_page_program_result = 3u;
        return;
    }
    /* 预读后检查全FF，非全FF不尝试编程，避免破坏原有数据。 */
    for (i = 0u; i < sizeof(expected); ++i)
    {
        if (g_page_readback[i] != 0xFFu)
        {
            g_page_program_result = 4u;
            return;
        }
        expected[i] = (uint8_t)i;
    }
    //尝试编程16字节，地址0xFFF000，数据为00～0F，最大查询次数100000。
    if (!w25q_page_program(1u, 0xFFF000u, expected, sizeof(expected), 100000u))
    {
        g_page_program_result = 5u;
        return;
    }
    //编程后读回16字节，保存到g_page_readback，如果读取失败则设置g_page_program_result为6并返回
    if (!w25q_read_data(1u, 0xFFF000u, g_page_readback, sizeof(g_page_readback)))
    {
        g_page_program_result = 6u;
        return;
    }
    //比对读回数据与预期数据，统计不一致的字节数，保存到g_page_mismatch，如果不一致则设置g_page_program_result为7，否则为1
    for (i = 0u; i < sizeof(expected); ++i)
    {
        if (g_page_readback[i] != expected[i])
        {
            ++mismatch;
        }
    }
    g_page_mismatch = mismatch;
    g_page_program_result = (mismatch == 0u) ? 1u : 7u;
}
