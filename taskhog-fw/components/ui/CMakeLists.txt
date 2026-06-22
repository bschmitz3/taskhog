        idf_component_register(
            SRCS
                "epaper_drv.c"
                "assets_fonts.c"
                "assets_images.c"
                "framebuffer.c"
                "widgets.c"
                "screens.c"
            INCLUDE_DIRS "."
            REQUIRES board driver power storage esp_timer freertos
        )
