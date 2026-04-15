function(make_firmware board board_def)
    add_executable(${board}
        main.c slider.c rgb.c button.c savedata.c config.c commands.c
        cli.c mpr121.c hebtn.c lzfx.c usb_descriptors.c gesture.c
        ps4key.c)
    target_compile_definitions(${board} PUBLIC ${board_def})
    pico_enable_stdio_usb(${board} 1)
    pico_enable_stdio_uart(${board} 0)

    pico_generate_pio_header(${board} ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
    
    target_compile_options(${board} PRIVATE -Wall -Werror -Wfatal-errors -O3)
    target_include_directories(${board} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    
    target_link_libraries(${board} PRIVATE
        pico_multicore pico_stdlib hardware_pio hardware_flash
        hardware_adc hardware_i2c hardware_watchdog pico_rand pico_mbedtls
        tinyusb_device tinyusb_board)

    pico_add_extra_outputs(${board})

    add_custom_command(TARGET ${board} PRE_BUILD
    COMMAND touch ${CMAKE_CURRENT_SOURCE_DIR}/cli.c)

    add_custom_command(TARGET ${board} POST_BUILD
                       COMMAND cp ${board}.uf2 ${CMAKE_CURRENT_LIST_DIR}/..)
endfunction()

make_firmware(diva_pico BOARD_DIVA_PICO)

