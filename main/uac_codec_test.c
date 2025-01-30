/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h> 
#include <stdio.h>
#include <string.h>
#include "usb_uac.h"
#include "sdcard.h"
#include "uac_audio_player.h"
#include "esp_err.h"  
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "ram_task.h"
#include "usb/uac_host.h"
#include "conf.h"
#include "audio_task.h"

extern uac_host_device_handle_t s_spk_dev_handle;
extern QueueHandle_t audio_file_queue;
void app_main(void)
{
    start_info_task();

    mount_sd_card();

    uac_init();

    uac_audio_player_init();

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    uac_host_device_set_volume(s_spk_dev_handle, 30); 

    //send_mp3_files(sdcard_mount_point);
    xQueueSend(audio_file_queue, "/sdcard/1.mp3", portMAX_DELAY);
    vTaskDelay(30000 / portTICK_PERIOD_MS);
    xQueueSend(audio_file_queue, "/sdcard/2.mp3", portMAX_DELAY);

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}