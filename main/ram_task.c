#include "ram_task.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "string.h"

#define MAX_TASK_NAME_LEN 32
// 任务信息结构体，用于排序
static const char *TAG = "RAM_TASK";
typedef struct
{
    char taskName[MAX_TASK_NAME_LEN];
    UBaseType_t highWaterMark;
    uint32_t cpuUsage;
} TaskInfo;
// 输出内存状态信息
int compare_tasks_info(const void *a, const void *b)
{
    TaskInfo *taskA = (TaskInfo *)a;
    TaskInfo *taskB = (TaskInfo *)b;
    return taskB->highWaterMark - taskA->highWaterMark;
}
void print_ram_info()
{
    // 获取内部 RAM 的内存信息
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    size_t used_size = total_size - free_size;

    ESP_LOGI(TAG, "Internal RAM:");
    ESP_LOGI(TAG, "  Total size: %d kbytes", total_size);
    ESP_LOGI(TAG, "  Used size: %d kbytes", used_size);
    ESP_LOGI(TAG, "  Free size: %d kbytes", free_size);

    // 获取 PSRAM 的内存信息（如果可用）
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0)
    {
        free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
        used_size = total_size - free_size;

        ESP_LOGI(TAG, "PSRAM:");
        ESP_LOGI(TAG, "  Total size: %d kbytes", total_size);
        ESP_LOGI(TAG, "  Used size: %d kbytes", used_size);
        ESP_LOGI(TAG, "  Free size: %d kbytes", free_size);
    }
    else
    {
        ESP_LOGI(TAG, "PSRAM not available");
    }

    TaskStatus_t *taskStatusArray = NULL;
    UBaseType_t taskCount, index;
    uint32_t totalRunTime;

    // 获取当前任务数量
    taskCount = uxTaskGetNumberOfTasks();

    // 分配内存来存储任务状态
    taskStatusArray = (TaskStatus_t *)pvPortMalloc(taskCount * sizeof(TaskStatus_t));

    if (taskStatusArray != NULL)
    {
        // 获取任务状态信息
        taskCount = uxTaskGetSystemState(taskStatusArray, taskCount, &totalRunTime);

        if (taskCount > 0)
        {
            // 分配内存来存储排序后的任务信息
            TaskInfo *taskInfoArray = (TaskInfo *)pvPortMalloc(taskCount * sizeof(TaskInfo));
            // 打印表头
            printf("Task Name\t\tHigh Water Mark\tCPU Usage\n");
            printf("--------------------------------------------\n");

            // 打印每个任务的高水位线和 CPU 占用率
            for (index = 0; index < taskCount; index++)
            {
                // 获取任务的栈高水位线
                taskInfoArray[index].highWaterMark = uxTaskGetStackHighWaterMark(taskStatusArray[index].xHandle);

                // 计算 CPU 占用率
                taskInfoArray[index].cpuUsage = 0;
                if (totalRunTime > 0)
                {
                    taskInfoArray[index].cpuUsage = (taskStatusArray[index].ulRunTimeCounter * 100) / totalRunTime;
                }
                strncpy(taskInfoArray[index].taskName, taskStatusArray[index].pcTaskName, MAX_TASK_NAME_LEN);
            }
            qsort(taskInfoArray, taskCount, sizeof(TaskInfo), compare_tasks_info);
            for (index = 0; index < taskCount; index++)
            {
                // 打印任务信息
                printf("%-16s\t%u\t\t%lu%%\n",
                       taskInfoArray[index].taskName,
                       taskInfoArray[index].highWaterMark,
                       taskInfoArray[index].cpuUsage);
            }
        }
        else
        {
            printf("Failed to get task state information\n");
        }

        // 释放内存
        vPortFree(taskStatusArray);
    }
    else
    {
        printf("Failed to allocate memory for task status array\n");
    }
}
// info 刷新任务
void info_task(void *pvParameter)
{
    while (1)
    {
        print_ram_info();
        vTaskDelay(pdMS_TO_TICKS(10000)); // 每 10s 调用一次
    }
}

// 初始化 info 刷新任务
void start_info_task()
{
    portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();
    xTaskCreatePinnedToCore(info_task, "tasks info Task", 1024 * 3, NULL, 1, NULL, 1);
}