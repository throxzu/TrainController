idf_component_register(
    SRCS "motor_l298n.cpp"
    INCLUDE_DIRS "."
    REQUIRES train_config driver freertos
)
