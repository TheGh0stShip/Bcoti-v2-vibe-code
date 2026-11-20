idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_adc esp_wifi esp_http_server esp_timer Mini2 json
                    EMBED_FILES "index.html" "debug.html"
                    )
