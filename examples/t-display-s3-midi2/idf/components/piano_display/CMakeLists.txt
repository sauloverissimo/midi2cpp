# piano_display, ESP-IDF component for the on-board ST7789 1.9" 320x170
# parallel-8-bit display on the LilyGo T-Display S3. Renders a 25-key
# piano roll plus a minimal info bar; the recipe wires UMP NoteOn /
# NoteOff into set_note_active() and the render task in the recipe's
# board glue calls render_frame() at ~60 fps.
#
# Depends on LovyanGFX (managed via idf_component.yml).

idf_component_register(
    SRCS "piano_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lovyangfx
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
