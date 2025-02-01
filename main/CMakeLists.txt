set(COMPONENTS driver)
#target_link_libraries(${COMPONENT_TARGET} INTERFACE driver)


idf_component_register(
    SRCS "led_task.c" "uac_audio_player.c" "ram_task.c" "uac_codec_test.c" "sdcard.c" "usb_uac.c" "audio_task.c"
    INCLUDE_DIRS "." 
    REQUIRES fatfs usb vfs driver nvs_flash
)