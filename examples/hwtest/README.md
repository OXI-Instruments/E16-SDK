# Hardware test

Checks the SDK's drivers on a real E16. Install it, then go through the list
and note anything that looks different.

## On power-up

1. All rings light **red**, then **green**, then **blue**, half a second each.
   *Checks the LED colour order.*
2. All rings go dark, then one LED lights per ring, ring 1 to ring 16 in turn.
   *Checks ring numbering and where LED 0 sits on each ring.*

## Then

| Do this | You should see | Checks |
|---|---|---|
| Turn any encoder clockwise | The cyan dot on its ring moves clockwise, one LED per detent | Encoder wiring, direction, steps per detent |
| Turn it counter-clockwise | The dot moves back | Direction |
| Press an encoder | Its ring lights dim white while held | Encoder switches |
| Hold Shift | All dots turn orange and blink once per second | Shift button, clock speed (time 10 blinks: should be 10 s) |
| Hold Shift + press encoder 1 for 2 s | Ring 1 fills red, all rings flash red, the E16 restarts in bootloader mode ("OXI E16 FW Update") | `system_enter_bootloader()` |

To leave the test, install other firmware: hold Shift + encoder 1 as above,
or switch off and use the bootloader button combo.
