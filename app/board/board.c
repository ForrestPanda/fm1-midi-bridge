/* SPDX-License-Identifier: Apache-2.0 */
/* Derived from the JieLi AC79 SDK apps/demo/demo_ble (Apache-2.0). Modified for the M-VAVE FM-1. */
#include "app_config.h"

#include "system/includes.h"
#include "device/includes.h"
#include "asm/includes.h"
#if TCFG_USB_SLAVE_ENABLE
#include "usb/otg.h"
#include "usb/usb_config.h"
#endif

// *INDENT-OFF*


UART2_PLATFORM_DATA_BEGIN(uart2_data)
	.baudrate = 1000000,
	.port = PORT_REMAP,
	.output_channel = OUTPUT_CHANNEL0,
	.tx_pin = -1,            /* FM-1: PC3 (the demo's console pin) is undocumented: no open firmware touches
	                          * PC3/PC4/PC5 and there is no schematic, so nothing is driven. The console is the
	                          * RAM log read over SysEx (app/fm1_log.c). Set IO_PORTC_03 only with the pin traced. */
	.rx_pin = -1,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
	.flags = UART_DEBUG,
UART2_PLATFORM_DATA_END();

/* TRS MIDI IN: PH8 -> input channel 1 -> UART1 RX, 31250 baud (Felucca: fm1_uart.h).
 * No TX: the jack is input only. */
UART1_PLATFORM_DATA_BEGIN(uart1_data)
	.baudrate = 31250,
	.port = PORT_REMAP,
	.input_channel = INPUT_CHANNEL1,
	.output_channel = OUTPUT_CHANNEL1,
	.tx_pin = -1,
	.rx_pin = IO_PORTH_08,
	.max_continue_recv_cnt = 64,
	.idle_sys_clk_cnt = 48000,          /* ~1 ms at 48 MHz: a MIDI byte is 320 us */
	.clk_src = PLL_48M,
	.flags = UART_RX_USE_DMA,
UART1_PLATFORM_DATA_END();


#if TCFG_ADKEY_ENABLE

#define ADKEY_UPLOAD_R  22

#define ADC_VDDIO (0x3FF)
#define ADC_09   (0x3FF)
#define ADC_08   (0x3FF)
#define ADC_07   (0x3FF)
#define ADC_06   (0x3FF)
#define ADC_05   (0x3FF)
#define ADC_04   (0x3FF)
#define ADC_03   (0x3FF * 15  / (15  + ADKEY_UPLOAD_R))
#define ADC_02   (0x3FF * 10  / (10  + ADKEY_UPLOAD_R))
#define ADC_01   (0x3FF * 33  / (33  + ADKEY_UPLOAD_R * 10))
#define ADC_00   (0)

#define ADKEY_V_9      	((ADC_09 + ADC_VDDIO)/2)
#define ADKEY_V_8 		((ADC_08 + ADC_09)/2)
#define ADKEY_V_7 		((ADC_07 + ADC_08)/2)
#define ADKEY_V_6 		((ADC_06 + ADC_07)/2)
#define ADKEY_V_5 		((ADC_05 + ADC_06)/2)
#define ADKEY_V_4 		((ADC_04 + ADC_05)/2)
#define ADKEY_V_3 		((ADC_03 + ADC_04)/2)
#define ADKEY_V_2 		((ADC_02 + ADC_03)/2)
#define ADKEY_V_1 		((ADC_01 + ADC_02)/2)
#define ADKEY_V_0 		((ADC_00 + ADC_01)/2)

const struct adkey_platform_data adkey_data = {
    .enable     = 1,
	.adkey_pin  = IO_PORTB_01,
    .extern_up_en = 1,
	.ad_channel = 3,
    .ad_value = {
        ADKEY_V_0,
        ADKEY_V_1,
        ADKEY_V_2,
        ADKEY_V_3,
        ADKEY_V_4,
        ADKEY_V_5,
        ADKEY_V_6,
        ADKEY_V_7,
        ADKEY_V_8,
        ADKEY_V_9,
    },
    .key_value = {
        0,
        1,
        3,
        2,
        NO_KEY,
        NO_KEY,
        NO_KEY,
        NO_KEY,
        NO_KEY,
        NO_KEY,
    },
};

#endif


/************************** LOW POWER config ****************************/
static const struct low_power_param power_param = {
    .config         = TCFG_LOWPOWER_LOWPOWER_SEL,
    .btosc_disable  = TCFG_LOWPOWER_BTOSC_DISABLE,         //进入低功耗时BTOSC是否保持
    .vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,          //强VDDIO等级,可选：2.2V  2.4V  2.6V  2.8V  3.0V  3.2V  3.4V  3.6V
    .vddiow_lev     = TCFG_LOWPOWER_VDDIOW_LEVEL,          //弱VDDIO等级,可选：2.1V  2.4V  2.8V  3.2V
	.vdc14_dcdc 	= TRUE,	   							   //打开内部1.4VDCDC，关闭则用外部
    .vdc14_lev		= VDC14_VOL_SEL_LEVEL, 				   //VDD1.4V配置
	.sysvdd_lev		= SYSVDD_VOL_SEL_LEVEL,				   //内核、sdram电压配置
	.vlvd_enable	= TRUE,                                //TRUE电压复位使能
	.vlvd_value		= VLVD_SEL_25V,                        //低电压复位电压值
};

/************************** PWR config ****************************/
#define PORT_WAKEUP_IO			IO_PORTB_01					//软关机和休眠唤醒引脚
#define PORT_WAKEUP_NUM			(PORT_WAKEUP_IO/IO_GROUP_NUM)//默认:0-7:GPIOA-GPIOH, 可以指定0-7组

static const struct port_wakeup port0 = {
    .edge       = FALLING_EDGE,                            //唤醒方式选择,可选：上升沿\下降沿
    .attribute  = BLUETOOTH_RESUME,                        //保留参数
    .iomap      = PORT_WAKEUP_IO,                          //唤醒口选择
    .low_power	= POWER_SLEEP_WAKEUP|POWER_OFF_WAKEUP,    //低功耗IO唤醒,不需要写0
};

static const struct long_press lpres_port = {
    .enable 	= FALSE,
    .use_sec4 	= TRUE,										//enable = TRUE , use_sec4: TRUE --> 4 sec , FALSE --> 8 sec
    .edge		= FALLING_EDGE,								//长按方式,可选：FALLING_EDGE /  RISING_EDGE --> 低电平/高电平
    .iomap 		= PORT_WAKEUP_IO,							//长按复位IO和IO唤醒共用一个IO
};

static const struct sub_wakeup sub_wkup = {
    .attribute  = BLUETOOTH_RESUME,
};

static const struct charge_wakeup charge_wkup = {
    .attribute  = BLUETOOTH_RESUME,
};

static const struct wakeup_param wk_param = {
    /* FM-1: PB1 is the battery divider, not a button; no port wake-up until the
     * HOME key (in the 74HC595 matrix) is wired to the SDK key driver. */
    .sub = &sub_wkup,
    .charge = &charge_wkup,
    .lpres = &lpres_port,
};


#if defined CONFIG_BT_ENABLE || defined CONFIG_WIFI_ENABLE
#include "wifi/wifi_connect.h"
const struct wifi_calibration_param wifi_calibration_param = {
    .xosc_l     = 0xb,// 调节晶振左电容
    .xosc_r     = 0xb,// 调节晶振右电容
    .pa_trim_data  ={1, 7, 4, 7, 11, 1, 7},// 根据MP测试生成PA TRIM值
};
#endif

/* Panel: SPI1 port C = CLK PC9, DO PC10 (DI PC8 is reclaimed as the D/C GPIO by
 * app/ui/lcd.c). SPI mode 0, 12 MHz as Felucca runs it. */
SPI1_PLATFORM_DATA_BEGIN(spi1_data)
    .clk    = 12000000,
    .mode   = SPI_STD_MODE,
    .port   = 'C',
    .attr   = SPI_SCLK_L_UPH_SMPH | SPI_BIDIR_MODE,
SPI1_PLATFORM_DATA_END()

#if TCFG_USB_SLAVE_ENABLE
/* USB-C: device mode only (the FM-1 is a device; it has no host port) */
static const struct otg_dev_data otg_data = {
    .usb_dev_en = (TCFG_HUSB_EN << 1) | TCFG_FUSB_EN,
    .slave_online_cnt = 10,
    .slave_offline_cnt = 10,
    .detect_mode = OTG_SLAVE_MODE | OTG_CHARGE_MODE,
    .detect_time_interval = 50,
};
#endif

REGISTER_DEVICES(device_table) = {
	{"uart2", &uart_dev_ops, (void *)&uart2_data },
	{"uart1", &uart_dev_ops, (void *)&uart1_data },
	{"spi1", &spi_dev_ops, (void *)&spi1_data },
#if TCFG_USB_SLAVE_ENABLE
	{"otg", &usb_dev_ops, (void *)&otg_data },
#endif
};

#ifdef CONFIG_DEBUG_ENABLE
void debug_uart_init()
{
    uart_init(&uart2_data);
}
#endif

void board_early_init()
{
    devices_init();
}

static void board_power_init(void)
{
    power_init(&power_param);

    power_keep_state(POWER_KEEP_RESET);

    power_wakeup_init(&wk_param);
}

void board_init()
{
    board_power_init();
    adc_init();
#if TCFG_ADKEY_ENABLE
    key_driver_init();
#endif
    void cfg_file_parse(void);
    cfg_file_parse();
}

