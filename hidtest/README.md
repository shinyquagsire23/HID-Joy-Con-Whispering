# hidtest

This program will initialize two Joy-Con in a charging grip connected to a computer over USB-C, and then proceed to print out input packets to stdout. The program can also optionally dump both Joy-Con SPI flashes. Requires hidapi and is currently only tested on Linux.

## Compiling
`g++ -lhidapi-hidraw hidtest.cpp`

## Things to do

- Check for Joy-Con disconnects and reinitialize, along with this maybe have a thread which only reads incoming packets and sorts them accordingly?
- Port as a Linux HID driver?
