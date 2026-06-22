        idf_component_register(
            SRCS
                "es8311_codec.c"
                "audio_capture.c"
                "ring_buffer.c"
                "wav_writer.c"
                "audio_beep.c"
            INCLUDE_DIRS "."
            REQUIRES board codec_board esp_codec_dev freertos
        )
