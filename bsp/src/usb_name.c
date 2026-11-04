/*
 * Default USB product string. e16_add_firmware(<name> ... USB_NAME "<text>")
 * generates a translation unit that defines this symbol strongly, which the
 * linker prefers over this weak definition.
 */
const char *const e16_usb_product __attribute__((weak)) = "E16 Custom";
