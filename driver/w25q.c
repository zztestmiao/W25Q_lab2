#include "stm32f4xx.h"
#include "w25q.h"

/* 轮询次数上限，只用于避免无限等待，不是精确的毫秒计时。 */
#define W25Q_WAIT_LIMIT 100000u

/* 一次独立的写使能控制事务，不带地址和数据。
 * chip为1或2，command仅由下方06/04接口传入。
 * 调用前初始化SPI，并确认芯片空闲，禁止并发占用总线。
 * true仅表示SPI传输完成，必须另读SR1验证WEL。
 * 失败时释放片选并恢复STM32 SPI，不保证Flash状态恢复。 */
static bool w25q_send_write_control(uint8_t chip, uint8_t command)
{
    uint16_t cs_pin;
    uint8_t ignored;
    bool success;

    if (chip != 1u && chip != 2u)
    {
        return false;
    }
    cs_pin = (chip == 1u) ? GPIO_Pin_13 : GPIO_Pin_14;
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);
    success = w25q_transfer_byte(command, &ignored);
    /* 拉高CS结束命令，不能与后面的05查询合并片选。 */
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!success)
    {
        w25q_init();
    }
    return success;
}

/* 发送06，请求置位WEL。本身不擦除或编程。
 * 参数、前置条件及失败处理见上方辅助函数，调用后读SR1验证。 */
bool w25q_write_enable(uint8_t chip)
{
    return w25q_send_write_control(chip, 0x06u);
}

/* 发送04，请求清除WEL。调用后读SR1确认关闭成功。 */
bool w25q_write_disable(uint8_t chip)
{
    return w25q_send_write_control(chip, 0x04u);
}

bool w25q_read_data(uint8_t chip, uint32_t address,
                    uint8_t *buffer, uint32_t length)
{
    const uint32_t capacity = 16u * 1024u * 1024u;
    uint16_t cs_pin;
    uint8_t ignored;
    bool success = false;

    /* 先验证范围，再产生任何总线操作。
     * 用容量减地址判断剩余空间，避免address+length发生整数回绕。 */
    if ((chip != 1u && chip != 2u) || address >= capacity ||
        length > capacity - address || (length != 0u && buffer == 0))
    {
        return false;
    }
    if (length == 0u)
    {
        return true;
    }
    cs_pin = chip == 1u ? GPIO_Pin_13 : GPIO_Pin_14;
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);

    /* 普通读取命令0x03：紧跟三个地址字节，高字节在前。
     * 例如地址0x123456，依次发送0x12、0x34、0x56。
     * 本命令无需额外等待字节，地址发送后即可读取数据。 */
    if (!w25q_transfer_byte(0x03u, &ignored) ||
        !w25q_transfer_byte((uint8_t)(address >> 16), &ignored) ||
        !w25q_transfer_byte((uint8_t)(address >> 8), &ignored) ||
        !w25q_transfer_byte((uint8_t)address, &ignored))
    {
        goto finish;
    }
	
    for (uint32_t i = 0; i < length; ++i)
    {
        /* 芯片内部地址自动递增，不必为每个字节重新发送地址。
         * 0xFF仅用于提供时钟，返回值直接写入调用者的数组。 */
        if (!w25q_transfer_byte(0xFFu, &buffer[i]))
        {
            goto finish;
        }
    }
    success = true;

finish:
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!success)
    {
        w25q_init();
    }
    return success;
}

/* 读取状态寄存器1，不修改Flash内容。
 * 调用前先初始化SPI；chip只允许1或2，status必须指向有效变量。
 * 成功才更新输出。失败释放两根片选，并恢复STM32 SPI接口。
 * true仅说明收发完成，不能单独证明芯片存在或状态值可信。 */
bool w25q_read_status1(uint8_t chip, uint8_t *status)
{
    uint16_t cs_pin;
    uint8_t ignored;
    uint8_t received;
    bool success = false;

    if (status == 0 || (chip != 1u && chip != 2u))
    {
        return false;
    }
    
    cs_pin = (chip == 1u) ? GPIO_Pin_13 : GPIO_Pin_14;
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);

    /* 05指定状态寄存器1，不需要地址。命令阶段收到的字节丢弃。 */
    if (!w25q_transfer_byte(0x05u, &ignored))
    {
        goto finish;
    }

    /* FF提供8个时钟，接收状态。bit0是BUSY，bit1是WEL。
     * CS从发送命令到接收结束一直保持低，构成一次完整事务。 */
    if (!w25q_transfer_byte(0xFFu, &received))
    {
        goto finish;
    }
    *status = received;
    success = true;

finish:
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!success)
    {
        w25q_init();
    }
    return success;
}

bool w25q_read_jedec_id(uint8_t chip, uint32_t *id)
{
    uint16_t cs_pin;
    uint8_t ignored;
    uint8_t bytes[3];
    bool success = false;

    if (id == 0 || (chip != 1u && chip != 2u))
    {
        return false;
    }
    *id = 0u;
    cs_pin = (chip == 1u) ? GPIO_Pin_13 : GPIO_Pin_14;

    /* 两颗先都不选中，再只拉低目标芯片的CS。整个命令期间保持低。 */
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);

    /* 0x9F是读取JEDEC ID的命令，不需要地址或写使能。
     * 发命令时同时收到的字节没有ID意义，接收后丢弃。 */
    if (!w25q_transfer_byte(0x9Fu, &ignored))
    {
        goto finish;
    }
    for (uint32_t i = 0; i < 3u; ++i)
    {
        /* 发送0xFF只是提供读取时钟；三个返回字节依次是
         * 厂商、存储器类型、容量标识，不是写入Flash的数据。 */
        if (!w25q_transfer_byte(0xFFu, &bytes[i]))
        {
            goto finish;
        }
    }

    *id = ((uint32_t)bytes[0] << 16) |
          ((uint32_t)bytes[1] << 8) | bytes[2];
    success = true;

finish:
    /* 不论成功或失败，都释放片选，结束本次事务。 */
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!success)
    {
        /* 超时后重新初始化SPI，避免残留接收状态影响下一颗识别。
         * 只复位STM32的SPI接口，不修改Flash内容。 */
        w25q_init();
    }
    return success;
}

bool w25q_transfer_byte(uint8_t tx, uint8_t *rx)
{
    uint32_t remaining;

    if (rx == 0)
    {
        return false;
    }

    /* TXE为1表示发送缓冲区可接收新数据，不代表线上传输已经结束。 */
    remaining = W25Q_WAIT_LIMIT;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    {
        if (--remaining == 0u)
        {
            return false;
        }
    }

    /* 将字节交给SPI1，硬件自动产生8个时钟并输出该字节。
     * 全双工模式下，输出每一位的同时也从MISO采样一位。 */
    SPI_I2S_SendData(SPI1, tx);

    /* RXNE为1表示接收缓冲区已有一个字节。
     * 即使发送的是命令，也必须读走同时收到的字节，防止接收溢出。 */
    remaining = W25Q_WAIT_LIMIT;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET)
    {
        if (--remaining == 0u)
        {
            return false;
        }
    }
    *rx = (uint8_t)SPI_I2S_ReceiveData(SPI1);

    /* 本教学函数额外等待总线传输完成后返回。
     * 上层完成整条命令后再拉高CS，不在本函数内切换片选。 */
    remaining = W25Q_WAIT_LIMIT;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) != RESET)
    {
        if (--remaining == 0u)
        {
            return false;
        }
    }
    return true;
}

void w25q_init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

    /* 先预置输出为高，再配置输出模式，避免初始化时误选中芯片。 */
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOE, &gpio);

    /* PA5时钟、PA6接收、PA7发送，均连接到SPI1复用功能。 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);
    gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &gpio);

    SPI_I2S_DeInit(SPI1);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    /* 模式0：空闲时钟为低，在第一个时钟边沿采样。 */
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    /* 两颗芯片分别用GPIO片选，不使用SPI硬件NSS。 */
    spi.SPI_NSS = SPI_NSS_Soft;
    /* 先用低速排查通信：当前PCLK2为84MHz时，SCK约656kHz。 */
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);
}

/* 在有限次查询内等待Flash内部擦写空闲。
 * chip为1或2，max_polls必须大于0；调用前初始化SPI。
 * 每次查询都是独立05事务，不占用CS等待其他业务，也不修改Flash。
 * true表示某次成功读取且BUSY=0；false表示参数错误、收发失败
 * 或查询次数耗尽。次数不是毫秒，不能承诺精确超时时间。
 * 失败后Flash可能仍在擦写，不能据此贸然继续新命令或关闭其电源。
 * 当前用于启动阶段的阻塞教学驱动，不在中断或并发环境调用。
 * 未来正式OTA接入真实时间基准后，按手册最大擦写时间设置期限。 */
bool w25q_wait_ready(uint8_t chip, uint32_t max_polls)
{
    uint8_t status1;
    uint32_t poll;

    if ((chip != 1u && chip != 2u) || max_polls == 0u)
    {
        return false;
    }
    for (poll = 0u; poll < max_polls; ++poll)
    {
        if (!w25q_read_status1(chip, &status1))
        {
            return false;
        }
        /* 判断busy状态 如果是0则表示Flash空闲 可以返回true */
        if ((status1 & 0x01u) == 0u)
        {
            return true;
        }
    }
    return false;
}

/* 擦除一个4KiB扇区，永久改变该扇区所有4096字节。
 * address必须为扇区起点（低12位为0），范围0～0xFFF000。
 * 调用者必须确认区域用途、完成必要备份，先初始化SPI且独占总线。
 * max_polls为每阶段最多状态查询次数，不是毫秒。
 * true仅表示前置检查通过、擦除命令收发完成并最终读到BUSY=0，
 * 不保证芯片实际执行了擦除（例如保护位可能阻止操作），需读回验证。
 * false可能发生在命令已提交之后，不能假设原数据仍完好或Flash已空闲。
 * 本函数不自动重试，不在失败后继续发其他修改命令。 */
bool w25q_erase_sector(uint8_t chip, uint32_t address, uint32_t max_polls)
{
    uint16_t cs_pin;
    uint8_t status1;
    uint8_t ignored;
    bool sent;
    //如果chip不是1或2，或者address大于0xFFF000，或者address不是4KiB对齐，或者max_polls为0，则返回false
    if ((chip != 1u && chip != 2u) || address > 0xFFF000u ||
        (address & 0xFFFu) != 0u || max_polls == 0u)
    {
        return false;
    }
    //如果等待Flash空闲失败，或者写使能失败，则返回false
    if (!w25q_wait_ready(chip, max_polls) || !w25q_write_enable(chip))
    {
        return false;
    }
    /* 必须确认空闲且WEL置位，不能只认为06发送成功就允许擦除。 */
    //如果读取状态寄存器失败，则返回false
    if (!w25q_read_status1(chip, &status1))
    {
        return false;
    }
    //查询状态寄存器1的结果，只有当WEL位为1且BUSY位为0时，才允许擦除操作
    if ((status1 & 0x03u) != 0x02u)
    {
        return false;
    }
    cs_pin = (chip == 1u) ? GPIO_Pin_13 : GPIO_Pin_14;
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);
    /* 20后紧跟三字节地址，高字节在前，不携带待写数据。
     * 整个命令保持CS低，最后拉高CS才提交内部擦除。
     * 这样从地址开始的4kB区域将被擦除 */
    sent = w25q_transfer_byte(0x20u, &ignored) &&
           w25q_transfer_byte((uint8_t)(address >> 16), &ignored) &&
           w25q_transfer_byte((uint8_t)(address >> 8), &ignored) &&
           w25q_transfer_byte((uint8_t)address, &ignored);
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!sent)
    {
        /* 即使传输报告超时，芯片也可能已收到完整命令。
         * 这里只恢复STM32接口，不能宣称Flash操作已取消。 */
        w25q_init();
        return false;
    }
    return w25q_wait_ready(chip, max_polls);
}

/* 单次页编程，最多256字节且禁止跨页，不自动擦除。
 * chip为1或2，buffer有效且至少length字节，length必须非零。
 * 调用前确认目标可写、必要备份完成、接口已初始化且独占SPI。
 * true只表示命令及等待完成，必须另行读回比较。
 * false可能已经部分写入或仍忙，不自动重试，也不保证WEL已清零。
 * max_polls为每阶段查询次数，不是毫秒；禁止在中断调用。 */
bool w25q_page_program(uint8_t chip, uint32_t address,
                       const uint8_t *buffer, uint32_t length,
                       uint32_t max_polls)
{
    uint16_t cs_pin;
    uint8_t ignored;
    uint8_t status1;
    uint32_t i;
    bool sent;
    /* 先验证参数范围，再产生任何总线操作。
     * 用容量减地址判断剩余空间，避免address+length发生整数回绕。
     * 还要检查是否跨页：每页256字节，地址低8位加长度不能超过256。 */
    if ((chip != 1u && chip != 2u) || 
        buffer == 0 || length == 0u ||
        address >= 0x1000000u || 
        length > 0x1000000u - address ||
        length > 256u - (address & 0xFFu) || 
        max_polls == 0u)
    {
        return false;
    }
    /* 先等待Flash空闲，再发送写使能，最后确认WEL置位。
     * 不能只认为06发送成功就允许编程。 */
    if (!w25q_wait_ready(chip, max_polls) || !w25q_write_enable(chip))
    {
        return false;
    }
    /* 读取状态寄存器1，确认WEL=1且BUSY=0，才允许编程。
     * 这里不再查询WEL位是否清零，因为06之后可能被其他命令清除。 */
    if (!w25q_read_status1(chip, &status1) || (status1 & 0x03u) != 0x02u)
    {
        return false;
    }
    cs_pin = (chip == 1u) ? GPIO_Pin_13 : GPIO_Pin_14;
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    GPIO_ResetBits(GPIOE, cs_pin);
    /* 02后紧跟三字节地址，高字节在前，再发送真实待写数据。
     * 整个命令保持CS低，最后拉高CS才提交内部编程。
     * 这样从地址开始的length字节将被编程。 */
    sent = w25q_transfer_byte(0x02u, &ignored) &&
           w25q_transfer_byte((uint8_t)(address >> 16), &ignored) &&
           w25q_transfer_byte((uint8_t)(address >> 8), &ignored) &&
           w25q_transfer_byte((uint8_t)address, &ignored);
    /* 发送数据阶段，若中途失败则停止发送，后续字节不再尝试。
     * 发送时收到的同步字节读走后丢弃，避免接收溢出。 */
    for (i = 0u; sent && i < length; ++i)
    {
        /* 数据阶段发送真实待写字节，收到的同步字节读走后丢弃。 */
        sent = w25q_transfer_byte(buffer[i], &ignored);
    }
    /* 即便中途失败，拉高CS也可能提交已收到的部分数据。 */
    GPIO_SetBits(GPIOE, GPIO_Pin_13 | GPIO_Pin_14);
    if (!sent)
    {
        w25q_init();
        return false;
    }
    return w25q_wait_ready(chip, max_polls);
}

/* 跨页分段编程：根据当前地址的页内偏移，逐页调用单页函数。
 * chip为1或2；buffer须有效且至少包含length字节；length不能为0。
 * 调用者须先初始化SPI、确认目标可写并做好备份，独占SPI总线。
 * 不自动擦除。max_polls为每次单页阶段的最多查询次数，不是毫秒。
 * true表示全部分段完成，仍须读回比较；false时此前各页可能已写入，
 * 也可能仍忙，不回滚、不自动重试。禁止在中断中调用。 */
bool w25q_write_data(uint8_t chip, uint32_t address,
                      const uint8_t *buffer, uint32_t length,
                      uint32_t max_polls)
{
    uint32_t chunk;

    /* 整段范围先检查，避免先写前几页才发现后面越界。
     * 先拒绝越界地址，再用容量减地址，避免加法整数回绕。 */
    if ((chip != 1u && chip != 2u) || buffer == 0 || length == 0u ||
        address >= 0x1000000u || length > 0x1000000u - address || max_polls == 0u)
    {
        return false;
    }
    /* 逐页调用单页编程函数，直到全部数据写完。
     * 每次计算当前页剩余空间，取它与剩余数据长度的较小值作为本次写入量。 */
    while (length != 0u)
    {
        /* 先算本页剩余空间，再取它与剩余数据长度中的较小值。 */
        chunk = 256u - (address & 0xFFu);
        if (chunk > length)
        {
            chunk = length;
        }
        /* 单页函数负责独立的06、02和等待完成，不在这里直接拼SPI命令。 */
        if (!w25q_page_program(chip, address, buffer, chunk, max_polls))
        {
            return false;
        }
        /* 同步推进Flash目的地址、RAM源指针，并扣除已写长度。 */
        address += chunk;
        buffer += chunk;
        length -= chunk;
    }
    return true;
}
