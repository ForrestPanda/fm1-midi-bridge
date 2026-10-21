/* SPDX-License-Identifier: Apache-2.0 */
/* Derived from the JieLi AC79 SDK apps/demo/demo_ble (Apache-2.0). Modified for the M-VAVE FM-1. */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define __FLASH_SIZE__    (1 * 1024 * 1024)   /* FM-1: 1 MB SPI NOR */

#ifdef CONFIG_NO_SDRAM_ENABLE
#define __SDRAM_SIZE__    (0 * 1024 * 1024)
#else
#define __SDRAM_SIZE__    (2 * 1024 * 1024)
#endif

#define CONFIG_DEBUG_ENABLE                  //打印开关

#define RTOS_STACK_CHECK_ENABLE //是否启用定时检查任务栈
// #define MEM_LEAK_CHECK_ENABLE	//是否启用内存泄漏检查(需要包含mem_leak_test.h头文件)


//*********************************************************************************//
//                                    电源配置                                     //
//*********************************************************************************//
// #define CONFIG_LOW_POWER_ENABLE   /* off for now: MIDI on UART must never sleep */ 				//低功耗睡眠使能
#define TCFG_LOWPOWER_BTOSC_DISABLE			0
#ifdef CONFIG_LOW_POWER_ENABLE
#define TCFG_LOWPOWER_LOWPOWER_SEL			(RF_SLEEP_EN | SYS_SLEEP_EN | RF_FORCE_SYS_SLEEP_EN | SYS_SLEEP_BY_IDLE)
#else
#define TCFG_LOWPOWER_LOWPOWER_SEL			0
#endif
#define TCFG_LOWPOWER_VDDIOM_LEVEL			VDDIOM_VOL_32V       //强VDDIO电压档位，不要高于外部DCDC的电压
#define TCFG_LOWPOWER_VDDIOW_LEVEL			VDDIOW_VOL_21V       //弱VDDIO电压档位
#define VDC14_VOL_SEL_LEVEL			        VDC14_VOL_SEL_140V   //RF1.4V电压档位
#define SYSVDD_VOL_SEL_LEVEL				SYSVDD_VOL_SEL_126V  //内核电压档位值


//*********************************************************************************//
//                                  BT_BLE配置                                     //
//*********************************************************************************//
#ifdef CONFIG_BT_ENABLE

#define CONFIG_BT_RX_BUFF_SIZE  0
#define CONFIG_BT_TX_BUFF_SIZE  0

#define TCFG_USER_BLE_ENABLE                      1     //BLE功能使能
#define TCFG_USER_BT_CLASSIC_ENABLE               0     //经典蓝牙功能

#if TCFG_USER_BLE_ENABLE
//BLE DEMO选择
#define BT_NET_HID_EN                             0     //从机 hid
#define TRANS_DATA_EN                             1     //从机 传输数据
#define BT_NET_CENTRAL_EN                         0     //主机 client角色
#define TRANS_MULTI_BLE_EN                        0     //多机通讯
#define CONFIG_BLE_MESH_ENABLE                    0     //mesh测试demo
#define APP_NONCONN_24G				  			  0		//2.4g加密通讯

#if (TRANS_MULTI_BLE_EN + BT_NET_HID_EN + BT_NET_CENTRAL_EN + TRANS_DATA_EN  + CONFIG_BLE_MESH_ENABLE + APP_NONCONN_24G > 1)
#error "they can not enable at the same time,just select one!!!"
#endif

#define TCFG_BLE_SECURITY_EN                      1     //配对加密使能

#if TRANS_MULTI_BLE_EN
#define TRANS_MULTI_BLE_SLAVE_NUMS                2
#define TRANS_MULTI_BLE_MASTER_NUMS               2
#endif

#endif

#endif

#if CONFIG_BLE_MESH_ENABLE
#define TCFG_ADKEY_ENABLE             1      //AD按键
#else
#define TCFG_ADKEY_ENABLE             0      //AD按键
#endif
//#define SDTAP_DEBUG

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG    0
#else
#define LIB_DEBUG    1
#endif
#define CONFIG_DEBUG_LIB(x)         (x & LIB_DEBUG)

//*********************************************************************************//
//                                   USB (fm1-midi-bridge)                          //
//*********************************************************************************//
#define FM1_FW_VERSION                      "0.1.0"
#ifndef FM1_MINIMAL
#define FM1_MINIMAL                         0       /* 1 (make MINIMAL=1): first-install build, see app/minimal.c */
#endif
#ifndef FM1_MINIMAL_LCD
#define FM1_MINIMAL_LCD                     1       /* the minimal build still draws a status screen */
#endif
#define CONFIG_USER_DEBUG_ENABLE                    /* SDK log sink hook log_putbyte() is ours: app/fm1_log.c */
#define FM1_BLE_HOST_EN                     1       /* BLE host (central) mode, exclusive with peripheral */
#define TCFG_PC_ENABLE                      1       /* USB device ("PC") mode */
#define USB_PC_NO_APP_MODE                  2       /* start/stop USB directly on plug events, no app task */
#define TCFG_FUSB_EN                        1       /* full-speed USB0: the FM-1's USB-C */
#define TCFG_HUSB_EN                        0
#define USB_MALLOC_ENABLE                   1
#define MIDI_CLASS                          0x00000100   /* app/usb/usb_midi.h */
#define USB_DEVICE_CLASS_CONFIG             (MIDI_CLASS)
#define TCFG_USB_SLAVE_MIDI_ENABLE          1
#include "usb_std_class_def.h"
#include "usb_common_def.h"

#endif
