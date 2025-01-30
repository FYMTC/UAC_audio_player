idf_component_register(
    SRCS "uac_audio_player.c" "ram_task.c" "uac_codec_test.c" "sdcard.c" "usb_uac.c" "audio_task.c"
    INCLUDE_DIRS "." 
    REQUIRES fatfs usb vfs
)