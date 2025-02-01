#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_vfs_fat.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_dec_default.h"
#include "esp_mp3_dec.h"

#include "string.h"
#include "usb/uac_host.h"

extern uac_host_device_handle_t s_spk_dev_handle;
extern uint8_t player_volume;
bool uac_player_playing = false;
bool uac_decoder_closed = true;
static const char *TAG = "UAC PLAYER";
// 音频解码器句柄
esp_audio_dec_handle_t decoder;
// 音乐文件 队列句柄
QueueHandle_t audio_file_queue;
// 定义用于传递解码后音频数据的队列
QueueHandle_t audio_data_queue;
// 控制解码器播放文件的队列
QueueHandle_t audio_control_file_queue;
// 定义缓冲区大小
#define head_buffer_size 1024 * 500 // 缓存音乐文件头部信息，可能包含图片，所以存大一点
#define input_buffer_size 1024 * 12
#define out_fram_buffer_size 1024 * 4
// 定义音频任务堆栈大小
#define codec_TASK_STACK_SIZE 1024 * 3
#define player_TASK_STACK_SIZE 1024 * 2
#define countrol_player_TASK_STACK_SIZE 1024 * 3
// 根据文件扩展名获取音频类型
esp_audio_type_t get_audio_type_from_file(const char *file_path)
{
    const char *ext = strrchr(file_path, '.');
    if (ext == NULL)
    {
        return ESP_AUDIO_TYPE_UNSUPPORT;
    }

    if (strcmp(ext, ".aac") == 0)
    {
        return ESP_AUDIO_TYPE_AAC;
    }
    else if (strcmp(ext, ".mp3") == 0)
    {
        return ESP_AUDIO_TYPE_MP3;
    }
    else
    {
        return ESP_AUDIO_TYPE_UNSUPPORT;
    }
}
void audio_decoder_task(void *pvParameters)
{
    // 注册解码器
    esp_audio_dec_register_default();
    char file_path[256];
    while (1)
    {
        if (xQueueReceive(audio_control_file_queue, &file_path, portMAX_DELAY) == pdTRUE)
        {
            uac_player_playing = true;
            uac_decoder_closed = false;
            uac_host_device_set_mute(s_spk_dev_handle, false);
            uac_host_device_set_volume(s_spk_dev_handle, player_volume);
            ESP_LOGI(TAG, "Received file path: %s", file_path);

            // 根据文件扩展名选择解码器类型
            esp_audio_type_t audio_type = get_audio_type_from_file(file_path);
            if (audio_type == ESP_AUDIO_TYPE_UNSUPPORT)
            {
                ESP_LOGE(TAG, "Unsupported audio format: %s", file_path);
                uac_player_playing = false;
                uac_decoder_closed = true;
                uac_host_device_set_mute(s_spk_dev_handle, true);
                continue;
            }

            // 配置解码器
            esp_audio_dec_cfg_t dec_cfg = {
                .type = audio_type, // 根据文件扩展名设置解码器类型
                .cfg = NULL,        // 如果没有特殊配置，设置为 NULL
                .cfg_sz = 0         // 如果没有特殊配置，设置为 0
            };

            esp_audio_dec_handle_t decoder = NULL;
            // 1. 打开解码器
            esp_audio_err_t ret = esp_audio_dec_open(&dec_cfg, &decoder);
            if (ret != ESP_AUDIO_ERR_OK)
            {
                ESP_LOGE(TAG, "Failed to open audio decoder, error: %d", ret);
                uac_player_playing = false;
                uac_decoder_closed = true;
                uac_host_device_set_mute(s_spk_dev_handle, true);
                continue;
            }
            // 打开文件
            FILE *file = fopen(file_path, "rb");
            if (file == NULL)
            {
                ESP_LOGE(TAG, "Failed to open file: %s", file_path);
                uac_player_playing = false;
                uac_decoder_closed = true;
                uac_host_device_set_mute(s_spk_dev_handle, true);
                continue;
            }
            // 2. 准备输入数据和输出缓冲区
            uint8_t *input_buffer = (uint8_t *)heap_caps_malloc(input_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT);  // 输入缓冲区
            uint8_t *frame_data = (uint8_t *)heap_caps_malloc(out_fram_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT); // 初始输出缓冲区
            uint8_t *temp_buffer = (uint8_t *)heap_caps_malloc(input_buffer_size * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT);
            uint8_t *head_buffer = (uint8_t *)heap_caps_malloc(head_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT);

            uint32_t temp_buffer_len = 0;
            size_t bytes_read = 0;
            esp_audio_dec_in_raw_t raw;
            while (1)
            {
                if (bytes_read == 0)
                {
                    bytes_read = fread(head_buffer, 1, head_buffer_size, file);
                    if (ferror(file))
                    {
                        ESP_LOGE(TAG, "Error reading file: %s", file_path);
                        break;
                    }

                    raw.buffer = head_buffer;
                    raw.len = bytes_read;
                }
                else
                {
                    bytes_read = fread(input_buffer, 1, input_buffer_size, file);
                    if (ferror(file))
                    {
                        ESP_LOGE(TAG, "Error reading file: %s", file_path);
                        break;
                    }
                    memcpy(temp_buffer + temp_buffer_len, input_buffer, bytes_read);

                    raw.buffer = temp_buffer;
                    raw.len = temp_buffer_len + bytes_read;
                }
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = frame_data,
                    .len = out_fram_buffer_size,
                };
                // ESP_LOGI(TAG, "Read %zu, temp_buffer_len: %lu, raw.len: %lu", bytes_read, temp_buffer_len, raw.len);
                //  解码数据并放入队列
                while (raw.len > 1440)
                {
                    ret = esp_audio_dec_process(decoder, &raw, &out_frame);
                    if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
                    {
                        // 输出缓冲区不足，重新分配更大的缓冲区
                        uint8_t *new_frame_data = (uint8_t *)heap_caps_realloc(frame_data, out_frame.needed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT);
                        if (new_frame_data == NULL)
                        {
                            ESP_LOGE(TAG, "Failed to realloc output buffer");
                            break;
                        }
                        frame_data = new_frame_data;
                        out_frame.buffer = new_frame_data;
                        out_frame.len = out_frame.needed_size;
                        ret = ESP_AUDIO_ERR_CONTINUE;
                        continue;
                    }

                    if (ret != ESP_AUDIO_ERR_OK)
                    {
                        ESP_LOGE(TAG, "Failed to process audio data, error: %d", ret);
                        break;
                    }

                    // 将解码后的数据放入队列
                    if (xQueueSend(audio_data_queue, &out_frame, pdMS_TO_TICKS(1000)) != pdTRUE)
                    {
                        ESP_LOGE(TAG, "Failed to send audio data to queue");
                        break;
                    }

                    // 更新输入数据指针和长度
                    raw.buffer += raw.consumed;
                    raw.len -= raw.consumed;
                    // ESP_LOGI(TAG, "consumed: %lu, raw.len: %lu", raw.consumed, raw.len);

                    if (!uac_player_playing)
                    {
                        ESP_LOGI(TAG, "Player STOP play");
                        break;
                    }
                }
                if (!uac_player_playing)
                {
                    break;
                }
                if (raw.len > 0 && raw.len <= out_fram_buffer_size)
                {
                    memcpy(temp_buffer, raw.buffer, raw.len);
                    temp_buffer_len = raw.len;
                    // ESP_LOGI(TAG, "reset temp_buffer_len: %lu", temp_buffer_len);
                }
                else if (raw.len == 0)
                {
                    ESP_LOGI(TAG, "Finished process");
                    break;
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid raw.len: %lu", raw.len);
                    break;
                }
                if (feof(file))
                {
                    ESP_LOGI(TAG, "Finished reading file: %s", file_path);
                    break; // 文件读取完毕
                }
            }
            uac_host_device_set_mute(s_spk_dev_handle, true);
            //  关闭文件
            fclose(file);
            // 4. 获取解码器信息
            esp_audio_dec_info_t dec_info;
            ret = esp_audio_dec_get_info(decoder, &dec_info);
            if (ret == ESP_AUDIO_ERR_OK)
            {
                ESP_LOGI(TAG, "Sample rate: %" PRIu32 ", Channels: %u, Bits per sample: %u",
                         dec_info.sample_rate, dec_info.channel, dec_info.bits_per_sample);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to get decoder info, error: %d", ret);
            }

            // 5. 关闭解码器
            esp_audio_dec_close(decoder);
            vTaskDelay(pdMS_TO_TICKS(1000)); // 延迟一下再清理，以免出现噪音

            // 释放资源（PSRAM 中的内存
            if (head_buffer)
            {
                heap_caps_free(head_buffer);
            }
            if (frame_data)
            {
                heap_caps_free(frame_data);
            }
            if (input_buffer)
            {
                heap_caps_free(input_buffer);
            }
            if (temp_buffer)
            {
                heap_caps_free(temp_buffer);
            }
            uac_player_playing = false;
            uac_decoder_closed = true;
        }
    }
}
void audio_player_task(void *pvParameters)
{
    while (1)
    {
        esp_audio_dec_out_frame_t out_frame;
        if (xQueueReceive(audio_data_queue, &out_frame, portMAX_DELAY) == pdTRUE)
        {
            if (out_frame.buffer)
            {
                esp_err_t write_ret = uac_host_device_write(s_spk_dev_handle, out_frame.buffer, out_frame.decoded_size, portMAX_DELAY);
                // ESP_LOGI(TAG, "decoded_size: %lu", out_frame.decoded_size);
                if (write_ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to write audio data to device, error: %d", write_ret);
                }
            }
        }
    }
}
void audio_control_task(void *pvParameters)
{
    char new_file_path[256];
    while (1)
    {
        if (xQueueReceive(audio_file_queue, &new_file_path, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "audio_control_task Received new file path: %s,uac_player_playing:%s", new_file_path, (uac_player_playing ? "true" : "false"));

            if (uac_player_playing)
            {
                
                uint8_t volume;
                uac_host_device_get_volume(s_spk_dev_handle, &volume);
                // 渐出效果，逐渐降低音量
                for (uint8_t v = volume; v > 0; v -= 1)
                {
                    uac_host_device_set_volume(s_spk_dev_handle, v);
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                uac_host_device_set_mute(s_spk_dev_handle, true);
                uac_player_playing = false;
            }
            ESP_LOGI(TAG, "uac_decoder_closed:%s", (uac_decoder_closed ? "true" : "false"));
            // Send the new file path to the decoder task
            if (xQueueSend(audio_control_file_queue, &new_file_path, pdMS_TO_TICKS(1000)) != pdTRUE)
            {
                ESP_LOGE(TAG, "Failed to send new file path to audio_control_file_queue");
            }
        }
    }
}
void uac_audio_player_init(void)
{

    // 创建音乐文件队列
    audio_file_queue = xQueueCreate(5, sizeof(char[256]));
    if (audio_file_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // 创建解码后音频数据队列
    audio_data_queue = xQueueCreate(5, sizeof(esp_audio_dec_out_frame_t));
    if (audio_data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create audio data queue");
        return;
    }

    audio_control_file_queue = xQueueCreate(5, sizeof(char[256]));
    if (audio_control_file_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    TaskHandle_t decoder_task_handle = NULL;
    TaskHandle_t player_task_handle = NULL;
    TaskHandle_t control_task_handle = NULL;
    // 创建解码任务
    xTaskCreatePinnedToCore(audio_decoder_task, "audio_decoder_task", codec_TASK_STACK_SIZE, NULL, 3, &decoder_task_handle, 1);
    // 创建播放任务
    xTaskCreatePinnedToCore(audio_player_task, "audio_player_task", player_TASK_STACK_SIZE, NULL, 3, &player_task_handle, 1);
    // 创建解码控制任务
    xTaskCreatePinnedToCore(audio_control_task, "audio_control_task", countrol_player_TASK_STACK_SIZE, NULL, 5, &control_task_handle, 1);
}