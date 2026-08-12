idf_component_register(
    SRCS "app_main.cpp" "turnout_control.cpp" "GeneralUtils.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        I2C PCF8574 motor_l298n train_config
        protocol_examples_common
        esp_wifi esp_netif esp_event nvs_flash
        mqtt esp_system log driver lwip
        freertos heap espressif__json_parser
        esp_https_ota app_update
)
