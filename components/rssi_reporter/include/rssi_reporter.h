/**
 * @file    rssi_reporter.h
 * @brief   RSSI信号强度上报模块
 */

#ifndef RSSI_REPORTER_H
#define RSSI_REPORTER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  启动RSSI上报任务
 *
 * 创建后台任务，定期上报WiFi信号强度和设备状态
 */
void rssi_reporter_start(void);

#ifdef __cplusplus
}
#endif

#endif // RSSI_REPORTER_H
