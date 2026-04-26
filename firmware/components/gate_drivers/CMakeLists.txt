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
    INCLUDE_DIRS
        "include"
    REQUIRES
        # Low-level system primitives every driver needs.
        driver         # gpio, ledc
        esp_timer
)
