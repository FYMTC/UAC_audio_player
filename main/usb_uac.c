#include "usb_uac.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include <inttypes.h>// 包含 PRIu32 宏
#include "string.h"

static const char *TAG = "UAC HOST";
// 定义USB主机任务的优先级为5
#define USB_HOST_TASK_PRIORITY 5
// 定义USB音频类（UAC）任务的优先级为5
#define UAC_TASK_PRIORITY 5
// 定义用户任务的优先级为2
#define USER_TASK_PRIORITY 2

// 定义BIT1_SPK_START宏，用于表示扬声器启动的位掩码，0x01左移0位，即0x01
#define BIT1_SPK_START (0x01 << 0)
// 定义默认的USB音频类（UAC）采样频率为48000Hz
#define DEFAULT_UAC_FREQ 48000
// 定义默认的USB音频类（UAC）位深度为16位
#define DEFAULT_UAC_BITS 16
// 定义默认的USB音频类（UAC）通道数为2（立体声）
#define DEFAULT_UAC_CH 2
// 定义USB主机任务堆栈大小为3KB
#define USB_HOST_TASK_STACK_SIZE 1024 * 3
// 定义USB音频类（UAC）任务堆栈大小为3KB
#define UAC_TASK_STACK_SIZE 1024 * 3
// 定义USB音频类主机任务堆栈大小为3KB
#define USB_UAC_Host_STACK_SIZE 1024 * 3


static QueueHandle_t s_event_queue = NULL;          // 事件队列
uac_host_device_handle_t s_spk_dev_handle = NULL;   // USB音频设备句柄
static uint32_t s_spk_curr_freq = DEFAULT_UAC_FREQ; // 当前扬声器采样频率
static uint8_t s_spk_curr_bits = DEFAULT_UAC_BITS;  // 当前扬声器位深度
static uint8_t s_spk_curr_ch = DEFAULT_UAC_CH;      // 当前扬声器通道数

// USB音频设备回调函数声明
static void uac_device_callback(uac_host_device_handle_t uac_device_handle, const uac_host_device_event_t event, void *arg);

/**
 * @brief 事件组枚举
 *
 * APP_EVENT            - 通用控制事件
 * UAC_DRIVER_EVENT     - UAC主机驱动事件，如设备连接
 * UAC_DEVICE_EVENT     - UAC主机设备事件，如接收/发送完成，设备断开
 */
typedef enum
{
    APP_EVENT = 0,
    UAC_DRIVER_EVENT,
    UAC_DEVICE_EVENT,
} event_group_t;

/**
 * @brief 事件队列结构体
 *
 * 用于将UAC主机事件从回调函数传递到uac_lib_task
 */
typedef struct
{
    event_group_t event_group; // 事件组
    union
    {
        struct
        {
            uint8_t addr;                  // 设备地址
            uint8_t iface_num;             // 接口号
            uac_host_driver_event_t event; // 驱动事件
            void *arg;                     // 参数
        } driver_evt;                      // 驱动事件结构体
        struct
        {
            uac_host_device_handle_t handle; // 设备句柄
            uac_host_driver_event_t event;   // 驱动事件
            void *arg;                       // 参数
        } device_evt;                        // 设备事件结构体
    };
} s_event_queue_t;


/**
 * @brief USB音频设备回调函数
 *
 * @param uac_device_handle 设备句柄
 * @param event 事件类型
 * @param arg 参数
 */
static void uac_device_callback(uac_host_device_handle_t uac_device_handle, const uac_host_device_event_t event, void *arg)
{
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED)
    { // 设备断开事件
        // 先停止音频播放器
        s_spk_dev_handle = NULL;
        // audio_player_stop(); // 停止播放
        ESP_LOGI(TAG, "UAC Device disconnected");
        ESP_ERROR_CHECK(uac_host_device_close(uac_device_handle)); // 关闭设备
        return;
    }
    // 将UAC设备事件发送到事件队列
    s_event_queue_t evt_queue = {
        .event_group = UAC_DEVICE_EVENT,
        .device_evt.handle = uac_device_handle,
        .device_evt.event = event,
        .device_evt.arg = arg};
    // 此处不应阻塞
    xQueueSend(s_event_queue, &evt_queue, 0);
}

/**
 * @brief USB主机库回调函数
 *
 * @param addr 设备地址
 * @param iface_num 接口号
 * @param event 事件类型
 * @param arg 参数
 */
static void uac_host_lib_callback(uint8_t addr, uint8_t iface_num, const uac_host_driver_event_t event, void *arg)
{
    // 将UAC驱动事件发送到事件队列
    s_event_queue_t evt_queue = {
        .event_group = UAC_DRIVER_EVENT,
        .driver_evt.addr = addr,
        .driver_evt.iface_num = iface_num,
        .driver_evt.event = event,
        .driver_evt.arg = arg};
    xQueueSend(s_event_queue, &evt_queue, 0);
}

/**
 * @brief 启动USB主机并处理常见的USB主机库事件
 *
 * @param arg 未使用
 */
static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config)); // 安装USB主机
    ESP_LOGI(TAG, "USB Host installed");
    xTaskNotifyGive(arg); // 通知任务

    while (true)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags); // 处理USB主机事件
        // 在此示例中，只有一个客户端注册
        // 因此，一旦我们注销客户端，此调用必须成功返回ESP_OK
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_ERROR_CHECK(usb_host_device_free_all()); // 释放所有设备
            break;
        }
    }

    ESP_LOGI(TAG, "USB Host shutdown");
    // 清理USB主机
    vTaskDelay(10);                        // 短暂延迟以允许客户端清理
    ESP_ERROR_CHECK(usb_host_uninstall()); // 卸载USB主机
    vTaskDelete(NULL);                     // 删除任务
}

/**
 * @brief UAC库任务
 *
 * @param arg 未使用
 */
static void uac_lib_task(void *arg)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // 等待通知
    uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = UAC_TASK_PRIORITY,
        .stack_size = USB_UAC_Host_STACK_SIZE,
        .core_id = 0,
        .callback = uac_host_lib_callback,
        .callback_arg = NULL};

    ESP_ERROR_CHECK(uac_host_install(&uac_config)); // 安装UAC驱动
    ESP_LOGI(TAG, "UAC Class Driver installed");
    s_event_queue_t evt_queue = {0};
    while (1)
    {
        if (xQueueReceive(s_event_queue, &evt_queue, portMAX_DELAY))
        { // 接收事件
            if (UAC_DRIVER_EVENT == evt_queue.event_group)
            { // 驱动事件
                uac_host_driver_event_t event = evt_queue.driver_evt.event;
                uint8_t addr = evt_queue.driver_evt.addr;
                uint8_t iface_num = evt_queue.driver_evt.iface_num;
                switch (event)
                {
                case UAC_HOST_DRIVER_EVENT_TX_CONNECTED:
                { // 发送连接事件
                    uac_host_dev_info_t dev_info;
                    uac_host_device_handle_t uac_device_handle = NULL;
                    const uac_host_device_config_t dev_config = {
                        .addr = addr,
                        .iface_num = iface_num,
                        .buffer_size = 16000,
                        .buffer_threshold = 4000, 
                        .callback = uac_device_callback,
                        .callback_arg = NULL,
                    };
                    ESP_ERROR_CHECK(uac_host_device_open(&dev_config, &uac_device_handle));  // 打开设备
                    ESP_ERROR_CHECK(uac_host_get_device_info(uac_device_handle, &dev_info)); // 获取设备信息
                    ESP_LOGI(TAG, "UAC Device connected: SPK");
                    uac_host_printf_device_param(uac_device_handle); // 打印设备参数
                    // 使用默认配置启动USB扬声器
                    const uac_host_stream_config_t stm_config = {
                        .channels = s_spk_curr_ch,
                        .bit_resolution = s_spk_curr_bits,
                        .sample_freq = s_spk_curr_freq,
                    };
                    esp_err_t err = uac_host_device_start(uac_device_handle, &stm_config); // 启动设备
                    if (err == ESP_ERR_NOT_SUPPORTED)
                    {
                        ESP_LOGE(TAG, "Unable to claim Interface, error: %s", esp_err_to_name(err)); // 接口不支持
                        // 处理错误，可能重试或中止操作
                        return;
                    }
                    ESP_ERROR_CHECK(err);
                    s_spk_dev_handle = uac_device_handle; // 更新设备句柄
                    // xQueueSend(audio_file_queue, MOUNT_POINT MP3_FILE_NAME, portMAX_DELAY);// 发送文件路径

                    break;
                }
                case UAC_HOST_DRIVER_EVENT_RX_CONNECTED:
                { // 接收连接事件
                    // 此示例不支持麦克风
                    ESP_LOGI(TAG, "UAC Device connected: MIC");
                    break;
                }
                default:
                    break;
                }
            }
            else if (UAC_DEVICE_EVENT == evt_queue.event_group)
            { // 设备事件
                uac_host_device_event_t event = evt_queue.device_evt.event;
                switch (event)
                {
                case UAC_HOST_DRIVER_EVENT_DISCONNECTED: // 设备断开事件
                    s_spk_curr_bits = DEFAULT_UAC_BITS;  // 重置位深度
                    s_spk_curr_freq = DEFAULT_UAC_FREQ;  // 重置采样率
                    s_spk_curr_ch = DEFAULT_UAC_CH;      // 重置通道数
                    ESP_LOGI(TAG, "UAC Device disconnected");
                    break;
                case UAC_HOST_DEVICE_EVENT_RX_DONE: // 接收完成事件
                    break;
                case UAC_HOST_DEVICE_EVENT_TX_DONE: // 发送完成事件
                    break;
                case UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR: // 传输错误事件
                    break;
                default:
                    break;
                }
            }
            else if (APP_EVENT == evt_queue.event_group)
            { // 应用事件
                break;
            }
        }
    }

    ESP_LOGI(TAG, "UAC Driver uninstall");
    ESP_ERROR_CHECK(uac_host_uninstall()); // 卸载UAC驱动
}



/**
 * @brief 主函数
 */
void uac_init(void)
{
    s_event_queue = xQueueCreate(10, sizeof(s_event_queue_t)); // 创建事件队列
    assert(s_event_queue != NULL);

    static TaskHandle_t uac_task_handle = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(uac_lib_task, "uac_events", UAC_TASK_STACK_SIZE, NULL,
                                             USER_TASK_PRIORITY, &uac_task_handle, 1); // 创建UAC任务
    assert(ret == pdTRUE);
    ret = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", USB_HOST_TASK_STACK_SIZE, (void *)uac_task_handle,
                                  USB_HOST_TASK_PRIORITY, NULL, 1); // 创建USB主机任务
    assert(ret == pdTRUE);
}