# uinputdriver

This program will create a single evdev controller from either two Joy-Con in a charging grip or a Pro Controller connected via USB Type-C.

## Compiling
`make`

## Things to do

- Check for Joy-Con disconnects and reinitialize, along with this maybe have a thread which only reads incoming packets and sorts them accordingly?
