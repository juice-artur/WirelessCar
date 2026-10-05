# STM OTA

## Flashing an application image

The bootloader speaks a strictly sequential, one-byte-status protocol over
USART1 at 115200 8N1. The device is put into OTA mode by pressing the user
button in the application, which sets `otaRequest` in the header page and
resets the MCU.

```
device -> host   0xA5   CMD_STM32_READY, once after reset
host   -> device 20 B   AppHeader_t, verbatim from the first 20 bytes of the .bin
device -> host   0x06   ASTRA_ACK, header accepted
                0x15   ASTRA_NACK, rejected; nothing was erased
                0xA6   ASTRA_ERASE_DONE, the slot is erased
                0x06   ASTRA_ACK, one Flash page was stored
                0x4F   ASTRA_IMAGE_OK, CRC matched and the header was committed
                0xFE   ASTRA_IMAGE_BAD, CRC mismatch
```

Build the application, which packs the header with `scripts/astra_image.py`, then
check the image without writing anything:

```powershell
python scripts/astra_ota.py --port COM7
```

Add `--flash` to actually write it:

```powershell
python scripts/astra_ota.py --port COM7 --flash
```

The body is sent one Flash page at a time and every page is acknowledged before
the next one is sent, because the receive buffer on the device is exactly one
page and is not re-armed until it has been consumed.

There is no retry. A lost acknowledgement is indistinguishable from a lost page,
so resending would corrupt the bytes that already landed in Flash; the script
stops instead and tells you to run it again.

`otaRequest` is cleared only by the commit at the very end, so an interrupted
transfer is always resumable by simply running the script again: the slot is
erased from scratch and the device re-announces itself on the next reset.

## ClangFormat

The repository uses the shared [`.clang-format`](.clang-format) configuration. VS Code is configured to use it through the Microsoft C/C++ extension for both STM32 projects.

Format project-owned C/C++ files from PowerShell:

```powershell
.\scripts\format.ps1
```

Check formatting without changing files:

```powershell
.\scripts\format.ps1 -Check
```

The script excludes generated/vendor paths (`Drivers`, `build`, and `cmake`). It uses `clang-format` from `PATH`, the binary bundled with `ms-vscode.cpptools`, or the executable specified by the `CLANG_FORMAT` environment variable.
