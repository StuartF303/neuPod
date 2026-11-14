idf_component_register(
    SRCS main.c results.c periph.c display.c radio.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_adc esp_lcd esp_psram esp_timer esp_wifi esp_netif esp_event
                  nvs_flash bt sdmmc spi_flash esp_hw_support heap esp_system
)
