#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "conf.h"
#include <dirent.h>
#include "audio_task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "usb/uac_host.h"

#include "driver/touch_pad.h" // 触摸传感器相关函数
#include "driver/gpio.h"      // GPIO 相关定义

#include "nvs_flash.h"           // NVS 相关头文件
#include "nvs.h"                 // NVS 相关头文件


#define MAX_PATH_LENGTH 256 // 文件路径最大长度
#define audio_task_stack_size 1024 * 3
#define TOUCH_THRESHOLD 100000           // 触摸阈值
#define NVS_NAMESPACE "mp3_player"   // NVS 命名空间
#define NVS_KEY_LAST_FILE "last_file" // NVS 中保存的键名

extern bool uac_player_playing;
extern QueueHandle_t audio_file_queue;

static const char *TAG = "MP3_PLAYER";
static char current_file_path[MAX_PATH_LENGTH]; // 当前播放的文件路径
static char base_path[MAX_PATH_LENGTH];         // 全局变量，存储音乐文件的基础路径
static bool loop_playback = false;                     // 是否开启循环播放
TaskHandle_t audio_task_handle = NULL;
// 初始化 NVS
void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 如果 NVS 分区被占用，擦除并重新初始化
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}
// 从 NVS 中读取上次播放的文件路径
bool read_last_file_from_nvs(char *file_path)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = MAX_PATH_LENGTH;
    err = nvs_get_str(nvs_handle, NVS_KEY_LAST_FILE, file_path, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read last file from NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Read last file from NVS: %s", file_path);
    return true;
}
// 将当前播放的文件路径保存到 NVS
void save_last_file_to_nvs(const char *file_path)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_LAST_FILE, file_path);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save last file to NVS: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Saved last file to NVS: %s", file_path);
    }

    nvs_commit(nvs_handle); // 提交更改
    nvs_close(nvs_handle);
}

// 查找下一个 MP3 文件
bool find_next_mp3_file(char *next_file_path)
{
    DIR *dir = opendir(base_path);
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory: %s", base_path);
        return false;
    }

    struct dirent *entry;
    bool found_current = false;
    bool result = false; // 用于标记是否找到下一个文件

    // 第一次遍历：查找当前文件的下一个文件
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        { // 如果是文件
            const char *file_name = entry->d_name;
            const char *ext = strrchr(file_name, '.');
            if (ext && strcmp(ext, ".mp3") == 0)
            { // 判断是否为 MP3 文件
                char file_path[MAX_PATH_LENGTH];
                snprintf(file_path, MAX_PATH_LENGTH, "%s/%s", base_path, file_name);

                if (found_current)
                {
                    // 找到当前文件的下一个 MP3 文件
                    strncpy(next_file_path, file_path, MAX_PATH_LENGTH);
                    result = true;
                    break;
                }

                if (strcmp(file_path, current_file_path) == 0)
                {
                    // 找到当前文件，标记为已找到
                    found_current = true;
                }
            }
        }
    }

    // 如果没有找到下一个文件，且开启了循环播放，则从头开始查找
    if (!result && loop_playback)
    {
        rewinddir(dir); // 重新遍历目录
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_REG)
            {
                const char *file_name = entry->d_name;
                const char *ext = strrchr(file_name, '.');
                if (ext && strcmp(ext, ".mp3") == 0)
                {
                    char file_path[MAX_PATH_LENGTH];
                    snprintf(file_path, MAX_PATH_LENGTH, "%s/%s", base_path, file_name);
                    strncpy(next_file_path, file_path, MAX_PATH_LENGTH);
                    result = true;
                    break;
                }
            }
        }
    }

    closedir(dir); // 确保目录只关闭一次
    return result;
}
void send_next_mp3_file()
{
    char next_file_path[MAX_PATH_LENGTH];
    if (find_next_mp3_file(next_file_path))
    {
        if (xQueueSend(audio_file_queue, next_file_path, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI(TAG, "Sent file to queue: %s", next_file_path);
            strncpy(current_file_path, next_file_path, MAX_PATH_LENGTH); // 更新当前文件路径
            save_last_file_to_nvs(next_file_path); // 保存当前播放路径到 NVS
        }
        else
        {
            ESP_LOGE(TAG, "Failed to send file to queue");
        }
    }
    else
    {
        ESP_LOGW(TAG, "No more MP3 files to play");
    }
}

void audio_task(void *param)
{
    while (1)
    {
        if (!uac_player_playing)
        {
            send_next_mp3_file(); // 发送下一个 MP3 文件
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每隔 1 秒检查一次
    }
}

void play_sdcard_mp3_files(const char *path, bool loop)
{
    struct stat path_stat;
    if (stat(path, &path_stat) != 0)
    {
        ESP_LOGE(TAG, "Failed to stat path: %s", path);
        return;
    }

    if (!S_ISDIR(path_stat.st_mode))
    {
        ESP_LOGE(TAG, "Path is not a directory: %s", path);
        return;
    }

    // 设置全局变量
    strncpy(base_path, path, MAX_PATH_LENGTH); // 存储基础路径
    memset(current_file_path, 0, MAX_PATH_LENGTH); // 重置当前文件路径
    loop_playback = loop; // 设置是否开启循环播放

    // 从 NVS 中读取上次播放的文件路径
    if (read_last_file_from_nvs(current_file_path))
    {
        // 检查文件是否存在
        struct stat file_stat;
        if (stat(current_file_path, &file_stat) == 0)
        {
            ESP_LOGI(TAG, "Resuming playback from: %s", current_file_path);
        }
        else
        {
            ESP_LOGW(TAG, "Last played file not found: %s", current_file_path);
            memset(current_file_path, 0, MAX_PATH_LENGTH); // 清空路径
        }
    }


    if (audio_task_handle == NULL)
    {
        xTaskCreate(audio_task, "send_audio_task", audio_task_stack_size, NULL, 5, NULL);
    }
}

void touch_task(void *param)
{
    // 初始化触摸传感器
    touch_pad_init();
    touch_pad_config(TOUCH_PAD_NUM);
    touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    while (1)
    {
        uint32_t touch_value;
        touch_pad_read_raw_data(TOUCH_PAD_NUM, &touch_value); // 读取触摸传感器的原始数据
        //ESP_LOGI(TAG, "Touch value: %lu", touch_value);

        if (touch_value > TOUCH_THRESHOLD)
        {
            ESP_LOGI(TAG, "Touch detected on GPIO %d", TOUCH_PAD_NUM);
            send_next_mp3_file();//播放下一首
            vTaskDelay(pdMS_TO_TICKS(500)); // 防抖延迟
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 每 100ms 检查一次触摸
    }
}