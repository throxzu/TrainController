idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES I2C PCF8574 detect_config protocol_examples_common
             mqtt nvs_flash esp_wifi esp_event esp_netif esp_adc
             esp_https_ota app_update
)
