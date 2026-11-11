#ifndef W25Q_H
#define W25Q_H

#include <stdbool.h>
#include <stdint.h>

/* 仅初始化通信接口，不读取、不写入、不擦除芯片。 */
void w25q_init(void);

/* 同时发送和接收一个字节。
 * tx：待发送字节；rx：保存收到字节的有效指针，不能为NULL。
 * 成功返回true；参数错误或等待超时返回false。
 * 不控制CS，不代表Flash已经识别成功。 */
bool w25q_transfer_byte(uint8_t tx, uint8_t *rx);

/* chip为1或2，分别使用PE13或PE14片选。
 * 成功把三个ID字节拼成24位数写入*id；false表示参数错误或超时。
 * true只表示收发完成，是否符合预期型号需要另外判断。 */
bool w25q_read_jedec_id(uint8_t chip, uint32_t *id);

/* 从外部Flash字节地址address读取length字节到buffer。
 * 地址从0开始，不是STM32内部Flash地址；范围为0～0xFFFFFF。
 * 非零长度时buffer必须有效并有足够空间；零长度返回true，不通信。
 * 失败可能已更新部分buffer，调用方必须检查返回值。 */
bool w25q_read_data(uint8_t chip, uint32_t address,
                    uint8_t *buffer, uint32_t length);

/* 读取状态寄存器1：chip为1或2，status为有效输出指针。
 * 成功返回true并更新*status，失败返回false且保留原输出值。
 * 调用前初始化SPI，不修改Flash内容，不能在中断中并发使用。 */
bool w25q_read_status1(uint8_t chip, uint8_t *status);

/* 独立发送06/04，开启或关闭写使能。chip为1或2。
 * 先初始化接口并确认芯片空闲，禁止并发调用。
 * true仅为通信完成，需05读回WEL；不擦除、不编程。 */
bool w25q_write_enable(uint8_t chip);
bool w25q_write_disable(uint8_t chip);

/* 最多查询max_polls次05，成功读到BUSY=0返回true。
 * chip为1或2，次数非零。参数错误、通信失败或次数耗尽返回false。
 * 先初始化SPI，禁止并发；此次数不是毫秒，失败不表示Flash已停止工作。 */
bool w25q_wait_ready(uint8_t chip, uint32_t max_polls);

/* 擦除整个4KiB扇区，调用前核实用途并备份，禁止并发。
 * address须4KiB对齐且不超过0xFFF000；max_polls非零且不是毫秒。
 * true表示命令流程完成且空闲，仍需读回验证。
 * false时可能已擦除、仍忙或WEL仍置位，禁止假设数据未变或盲目重试。 */
bool w25q_erase_sector(uint8_t chip, uint32_t address, uint32_t max_polls);

/* 单页编程：非空数据、长度1～256，且不跨当前页边界。
 * 先初始化接口并独占SPI，调用者负责确认可写和必要备份。
 * 不自动擦除，true仍需读回；false可能已部分写入或仍忙。
 * max_polls是查询次数，不是毫秒。 */
bool w25q_page_program(uint8_t chip, uint32_t address,
                       const uint8_t *buffer, uint32_t length,
                       uint32_t max_polls);

#endif
