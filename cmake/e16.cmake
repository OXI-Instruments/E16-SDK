find_program(E16_PYTHON NAMES python3 python)
# Cached so the functions below also see them in projects that add the SDK with add_subdirectory().
set(E16_TOOLS_DIR ${CMAKE_CURRENT_LIST_DIR}/../tools CACHE INTERNAL "")
set(E16_UPLOAD_TOOL ${E16_TOOLS_DIR}/e16_upload.py CACHE INTERNAL "")
set(E16_EXAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../examples CACHE INTERNAL "")

# e16_add_firmware(<name> <sources>... [USB_NAME "<text>"])
#
# Builds <name>.elf linked for the E16 bootloader, plus <name>.bin (what the
# bootloader flashes), <name>.hex (for SWD probes) and <name>.syx (update file).
# Adds a <name>-upload target that sends it to an E16 in bootloader mode.
#
# USB_NAME sets the USB product string the firmware shows to the host (e.g.
# in a MIDI port picker); without it, every firmware shows up as "E16 Custom"
# (bsp/src/usb_name.c). It generates a tiny source file that defines the
# weak default symbol strongly, which the linker then picks over the BSP's.
function(e16_add_firmware name)
    cmake_parse_arguments(E16_FW "" "USB_NAME" "" ${ARGN})
    add_executable(${name} ${E16_FW_UNPARSED_ARGUMENTS})
    if(DEFINED E16_FW_USB_NAME)
        set(usb_name_src ${CMAKE_CURRENT_BINARY_DIR}/${name}_usb_name.c)
        file(WRITE ${usb_name_src} "const char *const e16_usb_product = \"${E16_FW_USB_NAME}\";\n")
        target_sources(${name} PRIVATE ${usb_name_src})
    endif()
    set_target_properties(${name} PROPERTIES SUFFIX .elf)
    target_link_libraries(${name} PRIVATE e16_bsp)
    target_link_options(${name} PRIVATE
        -T${E16_LINKER_SCRIPT}
        -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map
        -Wl,--print-memory-usage
        # Force an early search for _sbrk, so the linker pulls the BSP's bounded
        # one (bsp/src/sbrk.c) out of libe16_bsp.a instead of the unbounded one
        # in libnosys.a: libnosys is only reached by the implicit link line
        # after e16_bsp, and by then the linker has already passed over
        # e16_bsp.a once without knowing _sbrk would be needed.
        -Wl,-u,_sbrk
    )
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${E16_LINKER_SCRIPT})

    set(out $<TARGET_FILE_DIR:${name}>/${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${out}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${name}> ${out}.hex
        VERBATIM
    )

    if(E16_PYTHON)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${E16_PYTHON} ${E16_UPLOAD_TOOL} syx ${out}.bin -o ${out}.syx
            VERBATIM
        )
        add_custom_target(${name}-upload
            COMMAND ${E16_PYTHON} ${E16_UPLOAD_TOOL} upload ${out}.bin
            DEPENDS ${name}
            USES_TERMINAL
            VERBATIM
        )
    endif()
endfunction()

# e16_add_ble_updater(<name> <app>)
#
# Builds <name>, a firmware for the main processor that installs <app> on the
# Bluetooth chip and shows the result. <app> is the nRF52832 application as
# .hex, .bin (starting at 0x26000) or OXI .syx Bluetooth update file. Install
# <name> like any firmware; afterwards press Shift and install your own.
function(e16_add_ble_updater name app)
    if(NOT E16_PYTHON)
        message(FATAL_ERROR "e16_add_ble_updater needs Python 3")
    endif()
    get_filename_component(app ${app} ABSOLUTE)
    set(image ${CMAKE_CURRENT_BINARY_DIR}/${name}_ble_image.bin)
    set(asm ${CMAKE_CURRENT_BINARY_DIR}/${name}_ble_image.S)

    add_custom_command(OUTPUT ${image}
        COMMAND ${E16_PYTHON} ${E16_TOOLS_DIR}/e16_ble_image.py ${app} -o ${image}
        DEPENDS ${app} ${E16_TOOLS_DIR}/e16_ble_image.py
        VERBATIM
    )
    file(WRITE ${asm}
        ".section .rodata.ble_image, \"a\"\n"
        ".global ble_image\n.global ble_image_end\n.balign 4\n"
        "ble_image:\n.incbin \"${image}\"\nble_image_end:\n"
    )
    set_source_files_properties(${asm} PROPERTIES OBJECT_DEPENDS ${image})

    e16_add_firmware(${name} ${E16_TOOLS_DIR}/ble_updater/main.c ${asm})
    target_include_directories(${name} PRIVATE ${E16_EXAMPLES_DIR}/common)
endfunction()
