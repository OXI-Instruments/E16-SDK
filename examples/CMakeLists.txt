include_directories(common)

e16_add_firmware(rainbow rainbow/main.c)
e16_add_firmware(hwtest hwtest/main.c)
e16_add_firmware(display display/main.c)
e16_add_firmware(midi midi/main.c)
e16_add_firmware(storage storage/main.c)
e16_add_firmware(ble_midi ble_midi/main.c ble_midi/ble_midi.c)

# USB functions of their own next to MIDI (usb.h), by Charles Vestal. Each
# example compiles the TinyUSB class it needs and switches it on in its own
# tusb_config_app.h.
e16_add_firmware(usb_disk usb_disk/main.c usb_disk/usb_msc.c ${E16_TINYUSB_DIR}/class/msc/msc_device.c)
target_include_directories(usb_disk PRIVATE usb_disk)
e16_add_firmware(usb_tone usb_tone/main.c usb_tone/usb_audio.c ${E16_TINYUSB_DIR}/class/audio/audio_device.c)
target_include_directories(usb_tone PRIVATE usb_tone)
target_link_libraries(usb_tone PRIVATE m)
e16_add_firmware(usb_cam usb_cam/main.c usb_cam/usb_video.c ${E16_TINYUSB_DIR}/class/video/video_device.c USB_NAME "E16 Cam")
target_include_directories(usb_cam PRIVATE usb_cam)
target_link_libraries(usb_cam PRIVATE m)

# The Bluetooth chip's side of ble_midi needs Nordic's nRF5 SDK 16.0.0 and
# GNU make, see ble_midi/README.md. Without the SDK only the main-processor
# part is built.
set(NRF5_SDK_ROOT "" CACHE PATH "nRF5 SDK 16.0.0, for the ble_midi example")
if(NRF5_SDK_ROOT)
    set(nrf_out ${CMAKE_CURRENT_BINARY_DIR}/ble_midi_nrf)
    file(GLOB nrf_sources ${CMAKE_CURRENT_SOURCE_DIR}/ble_midi/nrf/*)
    add_custom_command(OUTPUT ${nrf_out}/ble_midi_nrf.hex
        COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/ble_midi/nrf SDK_ROOT=${NRF5_SDK_ROOT} OUTPUT_DIRECTORY=${nrf_out}
        DEPENDS ${nrf_sources}
        VERBATIM
    )
    e16_add_ble_updater(ble_midi_nrf ${nrf_out}/ble_midi_nrf.hex)
endif()
