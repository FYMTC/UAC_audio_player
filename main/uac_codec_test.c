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
#include "led_task.h"
extern uac_host_device_handle_t s_spk_dev_handle;
extern QueueHandle_t audio_file_queue;

uint8_t player_volume = 100;
void app_main(void)
{
    init_nvs();

    start_info_task();

    mount_sd_card();

    uac_init();

    uac_audio_player_init();

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    //uac_host_device_set_volume(s_spk_dev_handle, 100);

    play_sdcard_mp3_files("/sdcard/MP3", true);

    xTaskCreate(touch_task, "touch_task", 3 * 1024, NULL, 1, NULL);

    xTaskCreate(led_app_main, "led_task", 3 * 1024, NULL, 1, NULL);

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}