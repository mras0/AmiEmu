# AmiEmu

Buggy OCS Amiga emulator and associated tools.

## Using

Clone the repository and build:

`mkdir build && cd build && cmake .. && cmake --build .`

Then run the emulator:

`amiemu -rom <path to rom file> -df0 <path to adf/dms/executable>`

Use `amiemu -?` to see a list of command line options.

## Avoiding

You should probably not use this emulator. Instead use [WinUAE](https://www.winuae.net/) for Windows, [FS-UAE](https://fs-uae.net/) for Linux or [vAmiga](https://dirkwhoffmann.github.io/vAmiga/) for macOS.
