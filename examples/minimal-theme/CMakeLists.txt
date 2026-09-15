if(CONFIG_ZMK_DONGLE_THEME_ENGINE)
  zephyr_library()
  zephyr_library_sources(src/minimal_theme.c)
endif()
