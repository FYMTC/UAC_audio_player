#include "sdcard.h"
static const char *TAG = "SD_CARD";
void mount_sd_card()
{
    esp_err_t ret;

    // 配置SD/MMC主机
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // 自定义SD/MMC插槽引脚配置
    sdmmc_slot_config_t slot_config = {
        .clk = SDMMC_CLK_GPIO,  // CLK信号引脚
        .cmd = SDMMC_CMD_GPIO,  // CMD信号引脚
        .d0 = SDMMC_DATA0_GPIO, // D0信号引脚
        .d1 = SDMMC_DATA1_GPIO, // D1信号引脚 (4线模式)
        .d2 = SDMMC_DATA2_GPIO, // D2信号引脚 (4线模式)
        .d3 = SDMMC_DATA3_GPIO, // D3信号引脚 (4线模式)
        .cd = SD_DET_PIN,       // 卡检测引脚
        .wp = SDMMC_SLOT_NO_WP, // 不使用写保护引脚
        .width = 4,             // 总线宽度 (1或4)
        .flags = 0,             // 额外标志
    };

    // 挂载文件系统
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount(sdcard_mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }

    // 打印SD/MMC卡信息
    sdmmc_card_print_info(stdout, card);
}