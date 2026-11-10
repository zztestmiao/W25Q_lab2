#include "stm32f4xx.h"
#include "w25q.h"

/* 轮询次数上限，只用于避免无限等待，不是精确的毫秒计时。 */
#define W25Q_WAIT_LIMIT 100000u

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
