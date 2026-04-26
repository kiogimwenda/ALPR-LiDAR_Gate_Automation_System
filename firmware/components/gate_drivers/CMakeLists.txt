# components/gate_drivers — first-party hardware drivers.
#
# This component will grow over Phase 4.4 sub-milestones: relay output,
# limit-switch input, W5500 ethernet wrapper, safety-beam interrupt, LED
# pattern renderer. The 4.4.1 skeleton ships a single version header so
# main/ has something to depend on and the component layout is exercised
# end-to-end (idf.py picks it up, cmake registers it, the linker pulls the
# resulting static library into the final image).

idf_component_register(
    SRCS
        "src/version.cpp"
        "src/relay.cpp"
        "src/limit_switch.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        # ESP-IDF v6 split the legacy `driver` component into per-
        # peripheral components. `esp_driver_gpio` provides the GPIO
        # configuration / level / interrupt APIs used here; future
        # sub-milestones will add esp_driver_ledc (LED PWM), esp_eth
        # (W5500 ethernet), esp_driver_spi (W5500 transport), etc.
        esp_driver_gpio
        esp_timer
        log
)
