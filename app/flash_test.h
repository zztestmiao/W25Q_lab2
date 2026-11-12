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

/* 主循环轮询手动请求，默认不执行。先确认备份与区域用途，
 * Watch设置g_sector_erase_request=1后仅擦第一颗最后一个扇区。
 * 每次上电最多尝试一次，独占SPI，不能在中断调用。
 * 将覆盖RAM快照为擦除后数据，失败禁止盲目重试。 */
void flash_test_erase_poll(void);

/* 主循环中单次响应手动写入请求，默认不执行。
 * 固定第一颗0xFFF000写16字节，仅目标全FF时允许，无自动擦除。
 * 不要同时触发擦除与编程；失败不重试。结果见g_page_program_result。
 * 当前复用试验区，正式使用前须明确区域用途和备份。 */
void flash_test_program_poll(void);

/* 手动跨页实验，默认不执行，每次上电只尝试一次。
 * request写1后在第一颗FFF0FA写300字节，先检查全FF和非法参数。
 * 不要同时触发其他实验，失败保留现场，不自动擦除或重试。 */
void flash_test_cross_poll(void);

/* 综合模拟下载：默认不执行，request手动1触发后擦除第一颗最后扇区，
 * 三块追加1000字节并验证全段及尾部；每次上电一次，不与其他实验混用。
 * 操作会覆盖此前测试数据，不联网、不提交READY。 */
void flash_test_download_poll(void);

#endif
