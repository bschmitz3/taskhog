        idf_component_register(
            SRCS
                "sdcard.c"
"queue.c"
"journal.c"
            INCLUDE_DIRS "."
            REQUIRES board config fatfs sdmmc driver
        )
