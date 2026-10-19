# WirelessCar

The repository contains two independent projects for the STM32G431CBT6 (Cortex-M4F):

- `AstraStm32/AstraBootloader` — bootloader in the Flash region `0x08000000` (24 KB).
- `AstraStm32/AstraApplication` — application in the Flash region `0x08006800` (98 KB).
- `AstraNRF/` — a separate Zephyr / nRF Connect SDK project (unrelated to STM32 debugging).

Build: **CMake ≥ 3.22 + Ninja + GNU Arm Embedded GCC**. Debug: **VS Code + Cortex-Debug** via **ST-Link + OpenOCD**.


## What the user needs to do (once per PC)

All settings are **machine-local**. The repository contains no absolute paths, so it works the same on any PC.

### 1. Install the VS Code extension

- **Cortex-Debug** (`marus25.cortex-debug`).

### 2. Add the GNU Arm toolchain to `PATH`

This is required both for the build (`cmake --preset Debug`) and for debugging (Cortex-Debug looks up `arm-none-eabi-gdb` via `PATH`).
`cmake/gcc-arm-none-eabi.cmake` explicitly states that `arm-none-eabi-` must be in `PATH`.

Windows (PowerShell) example — use the path where your toolchain is installed:

```powershell
$dir = "$env:LOCALAPPDATA\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin"
$p = [Environment]::GetEnvironmentVariable('Path','User')
if (($p -split ';') -notcontains $dir) {
    [Environment]::SetEnvironmentVariable('Path', $p.TrimEnd(';') + ';' + $dir, 'User')
}
```

Examples of typical toolchain locations:

- STM32CubeMX: `%LOCALAPPDATA%\stm32cube\bundles\gnu-tools-for-stm32\<version>\bin`
- STM32CubeIDE: `C:\ST\STM32CubeIDE_<version>\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.<version>...\tools\bin`

After changing `PATH`, **fully restart VS Code** (the current process does not see the new `PATH`).
Verify in a new terminal:

```powershell
Get-Command arm-none-eabi-gcc, arm-none-eabi-gdb
```

`cmake`, `ninja`, and `openocd` are also required in `PATH`.

### 3. Make sure Cortex-Debug can find gdb and OpenOCD

If `arm-none-eabi-gdb` is not in `PATH`, set the path in the **User** `settings.json` (`%APPDATA%\Code\User\settings.json`):

```json
"cortex-debug.armToolchainPath": "C:\\Users\\Juice\\AppData\\Local\\stm32cube\\bundles\\gnu-tools-for-stm32\\14.3.1+st.2\\bin"
```

If OpenOCD is not in `PATH` (VS Code shows `Failed to launch OpenOCD GDB Server: Error: spawn openocd ENOENT`), add the path to `openocd.exe` there as well:

```json
"cortex-debug.openocdPath": "C:\\Users\\Juice\\AppData\\Local\\Microsoft\\WinGet\\Packages\\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\\xpack-openocd-0.12.0-7\\bin\\openocd.exe"
```

> These are machine-local settings — **do not** add them to the repository.

### 4. Connect the ST-Link

Debugging uses SWD. OpenOCD 0.12+ uses the configs `interface/stlink.cfg` and `target/stm32g4x.cfg` (resolved by OpenOCD itself; no paths are needed in the repo).

## Build

Both projects are separate CMake configurations:

```powershell
# Bootloader
cmake --preset Debug ; cmake --build --preset Debug
# Application
cmake --preset Debug ; cmake --build --preset Debug
```

VS Code has ready-made tasks (`Terminal → Run Task`):

- `Build All (Bootloader + App)` — builds both images (it also runs automatically before debugging).

## Debugging Bootloader + App simultaneously

1. Open the repository in VS Code and press **F5**, choose the
   **"Debug Bootloader + App (ST-Link/OpenOCD)"** configuration.
2. Both projects are built automatically before launch (`preLaunchTask`).
3. The configuration programs **both** ELF images at their addresses (`loadFiles`) and loads the symbols of both images into a single GDB session (`symbolFiles`).

What this gives you:

- breakpoints work in both the bootloader and the application;
- you can step through the `JumpToApplication()`, `SCB->VTOR = APP_START_ADDR` handoff and onward into the application code;
- reset starts in the bootloader (the bootloader `main` is the default entry point).

Note: both images share the same RAM and define identically named functions (`main`, HAL, etc.). For ambiguous names, GDB picks the first loaded image; `file:line` breakpoints work correctly because the source paths differ.

## Portability

- The repository contains no absolute paths — only `${workspaceFolder}` and tool names from `PATH`.
- `.vscode/launch.json` and `.vscode/tasks.json` are committed; keep machine-local settings (toolchain/openocd paths) in the **User** `settings.json`.
- On a new PC, simply follow steps 1–4 above.
