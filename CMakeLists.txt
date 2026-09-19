zephyr_include_directories(include)
if(CONFIG_ZMK_DONGLE_SCREEN_ENGINE)
  if(CONFIG_SHIELD_DONGLE_SCREEN OR CONFIG_SHIELD_DONGLE_DISPLAY)
    message(FATAL_ERROR "dongle_screen_host cannot share screen ownership with dongle_screen/dongle_display")
  endif()
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/scripts/generate_raster_assets.py
    --lvgl ${ZEPHYR_LVGL_MODULE_DIR} --output ${CMAKE_CURRENT_BINARY_DIR}/dongle_raster_assets.h
    COMMAND_ERROR_IS_FATAL ANY)
  zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR})
  zephyr_library_compile_options(-O2 -ffp-contract=off)
  zephyr_library_sources(src/engine.c src/raster.c src/ui.c src/zmk_host.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_DONGLE_SCREEN_ENGINE_V2_PREVIEW
    src/v2/pack.c)
endif()
