# hidtest

This program will initialize two Joy-Con or one Pro Controller either over Bluetooth or connected to a computer over USB-C (requires a charging grip for Joy-Con), and then proceed to print out input packets to stdout. The program can also optionally dump SPI flashes, test vibration, write to SPI, or replay packets from a text file. Requires hidapi and is currently only tested on Linux.

## Compiling
`g++ -lhidapi-hidraw hidtest.cpp`

## Things to do

- Check for Joy-Con disconnects and reinitialize, along with this maybe have a thread which only reads incoming packets and sorts them accordingly?
- Port as a Linux HID driver?
