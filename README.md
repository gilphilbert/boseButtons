# Bose AWR-1 Button Controller

  

This contains the code for the button controller, run on an ATTiny261/461/861 located on the front of the Bose AWR-1 "upferb" project.

### The Top Panel
The top panel for the device contains 17 buttons in a button matrix. The matrix features four "columns" and five "rows". Each button is represented an an intersection between a column and a row:
  
| |Column A|Column B|Column C|Column D|
|:-|:----:|:----:|:----:|:----:|
|__Row 1__|`Four`|`Two`|`Three`|`Five`|
|__Row 2__|-|-|`Six`|-|
|__Row 3__|`Up`|`On/Off`|`Sleep`|`Down`|
|__Row 4__|`Alarm Mode`|`Clock Set`|`>`|`One`|
|__Row 5__|`AM/FM`|`Alarm Set`|`<`|`Aux`|

If you're wondering how this was discovered: A test meter, a spare evening and a *lot* of patience.

### Reading The Buttons
Each row requires a pulldown resistor. Use a 100KΩ resistor to pull each row pin to ground.
Pull up each "column", one at a time, then scan the rows. If a row reads as high then a button is depressed. It's possible to tell which button has been pressed by checking the column currently in a high state and which row reads high. For example, if the current column in a high state is `Column B` and the row reading high is `Row 4` then the button depressed is `Clock Set`. It's possible to detect when two buttons are concurrently pressed.

## How this code works
### Columns
At a high level, the code works as above. A timer is used, set to pull up / down the columns every 10ms in a constant loop:
* Pull up `Column A`
* Wait 10ms
* Pull down `Column A`
* Wait 10ms
* Pull up `Column B`
* ...

### Rows
Meanwhile, a pin change interrupt (PCINT) is used to detect when row changes state (either high or low). Since PCINTs do not detect _which_ pin was changed, the code then scans the rows to find out, comparing against the column that's currently high. This is logged. When a button is released, the ATTiny pulls up the IRQ pin.

### Communication
The ATTiny is an i2c slave. The code for the i2c slave is [usitwislave.h](https://github.com/eriksl/usitwislave). All credit goes to Erik Slagter. When the IRQ pin is pulled high, this is a notification to the i2c master that one or more buttons have been pressed. The master then makes an i2c request of the slave device (the ATTiny). The ATTiny encodes all buttons that have been pressed using a 32-bit integer, split into four (4) bytes. Each byte is sent to the host, which must reassemble them into a 32-bit integer to work out which button(s) is/are active.

### Encoding
The encoding is a simple bit-shift. Starting at the top left of the button panel the buttons are base-2 indexed. `Mode Set` is 1, `Mode` is 2, `Clock Set` is 4, `<` is 8, and so on. A reading of 128 would indicate `Three` has been pressed. A reading of 192 would indicated two buttons have been pressed, `Three` (128) and `Two` (64).

Sample code to use this can be found in the mainboard code for the Bose AWR-1 'upferb' project.