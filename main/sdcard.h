#pragma once
#include "esp_log.h"
#include "conf.h"

#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

void mount_sd_card();