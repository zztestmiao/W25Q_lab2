#ifndef FLASH_TEST_H
#define FLASH_TEST_H

/* 启动时单次实验接口。先由main完成板级及w25q_init初始化。
 * 按identify、read_data、read_status、wel顺序调用，禁止并发调用。
 * 结果保存在flash_test.c的Watch变量中，本教学接口不返回业务结果。
 * 不擦除、不页编程；wel会改变WEL并尝试在结束时关闭。 */

/* 读取两颗ID，为后续实验建立型号及通信检查结果。 */
void flash_test_identify(void);

/* 依赖识别结果，读取两颗地址1开始的16字节到Watch快照。
 * 失败或跳过时必须结合read_status判断数组是否有效。 */
void flash_test_read_data(void);

/* 依赖识别结果，保存两颗SR1快照，失败不覆盖状态字节。 */
void flash_test_read_status(void);

/* 依赖第一颗识别及SR1结果，且BUSY须为0。
 * 执行06→05→04→05，失败也尝试04清理，检查两个结果变量。
 * 无等待忙完成的能力，前提不满足时直接记录跳过。 */
void flash_test_wel(void);

/* 前四项实验后调用，只读第一颗最后一个4KiB扇区到RAM。
 * 结果及统计见g_sector_read_result和g_sector_non_ff。
 * 禁止把RAM快照当作已完成电脑备份，不修改Flash。 */
void flash_test_sector_read(void);

#endif
