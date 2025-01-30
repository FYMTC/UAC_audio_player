#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
#include "conf.h"
#include <dirent.h>
#include "audio_task.h"
#include "esp_err.h" 
#include "esp_log.h"
#define MAX_FILES 100       // 最大文件数量
#define MAX_PATH_LENGTH 256 // 文件路径最大长度
extern bool uac_player_playing;
extern QueueHandle_t audio_file_queue;
static const char* TAG = "MP3_PLAYER";
static char mp3_file_list[MAX_FILES][MAX_PATH_LENGTH]; // 存储文件路径列表
static int total_files = 0;                           // 文件总数
static int current_file_index = 0;                   // 当前文件索引
void send_mp3_files_to_queue() {
    if (total_files == 0) {
        ESP_LOGW(TAG, "No MP3 files to play");
        return;
    }

    if (!uac_player_playing) { // 仅当未播放时发送文件路径
        char* file_path = mp3_file_list[current_file_index];
        if (xQueueSend(audio_file_queue, file_path, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Sent file to queue: %s", file_path);

            // 更新当前索引，循环播放
            current_file_index = (current_file_index + 1) % total_files;
        } else {
            ESP_LOGE(TAG, "Failed to send file to queue");
        }
    }
}

void audio_task(void* param) {
    while (1) {
        send_mp3_files_to_queue(); // 依次发送文件
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每隔 1 秒检查一次
    }
}
void send_mp3_files(const char* base_path) {
    DIR* dir = opendir(base_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", base_path);
        return;
    }

    struct dirent* entry;
    total_files = 0; // 重置文件总数

    while ((entry = readdir(dir)) != NULL && total_files < MAX_FILES) {
        if (entry->d_type == DT_REG) { // 如果是文件
            const char* file_name = entry->d_name;
            const char* ext = strrchr(file_name, '.');
            if (ext && strcmp(ext, ".mp3") == 0) { // 判断是否为 MP3 文件
                if (strlen(base_path) + strlen(file_name) + 2 > MAX_PATH_LENGTH) { // 检查路径长度
                    ESP_LOGW(TAG, "File path too long, skipping: %s/%s", base_path, file_name);
                    continue;
                }
                snprintf(mp3_file_list[total_files], MAX_PATH_LENGTH, "%s/%s", base_path, file_name);
                ESP_LOGI(TAG, "Found MP3 file: %s", mp3_file_list[total_files]);
                total_files++;
            }
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Total MP3 files found: %d", total_files);
    xTaskCreate(audio_task, "send_audio_task", 4096, NULL, 5, NULL);
}
