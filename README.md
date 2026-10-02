# App-Shot

App-Shot is a command-line tool for Windows that captures an open application's window and saves it as a PNG. It can resize the window before capturing it to produce images with specific dimensions, useful for documentation, reports, or interface comparisons.

The application lays out its content at the new size before capture: **the image is not scaled afterward**. App-Shot captures a single window, not the entire desktop or a video.

The executable is named `window-shot.exe`.

## Features

- List visible windows with their index, PID, executable name, and title.
- Select a window by index, PID, or part of its title.
- Capture it at its current size or request an output size in pixels.
- Save the result as a PNG file, defaulting to `screenshot.png`.
- Capture without including overlapping windows by asking the selected window to render its own content through the Win32 `PrintWindow` API.

## Requirements and build

- Windows 10 or 11 with a desktop session.
- CMake 3.20 or later.
- Visual Studio or Build Tools with the **Desktop development with C++** workload and the Windows SDK; C++20 support is required.

From PowerShell, in the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

With the Visual Studio generator, the executable is created at `build\Release\window-shot.exe`. Capture uses Win32, GDI+, GDI, and DWM; no third-party libraries are required.

## Usage

First, open the application whose window you want to capture.

```powershell
# List available windows
.\build\Release\window-shot.exe --list

# Capture a window by part of its title, without resizing it
.\build\Release\window-shot.exe --title "My application" --out capture.png

# Resize the window to produce a 1280 × 720 pixel PNG
.\build\Release\window-shot.exe --title "My application" --size 1280x720 --out capture-720p.png

# Select by the index shown in --list
.\build\Release\window-shot.exe --index 2 --out window.png

# Select by PID; replace 1234 with the window's PID
.\build\Release\window-shot.exe --pid 1234 --out window.png

# Combine PID and title to distinguish windows belonging to the same process
.\build\Release\window-shot.exe --pid 1234 --title "Document" --out document.png
```

### Options

| Option | Behavior |
| --- | --- |
| `--list` | Lists visible windows with a title, excluding child and tool windows. Does not capture an image. |
| `--index N` | Selects a row from the list, starting at 1. The list is sorted by executable name, title, and PID; indices may change when windows are opened or closed. Takes precedence over other selectors. |
| `--pid PID` | Matches a full PID or a numeric substring of the PID. Can be combined with `--title`. |
| `--title TEXT` | Matches part of the title, case-insensitively. |
| `--size WIDTHxHEIGHT` | Requests the final PNG dimensions in pixels. Changes the actual window size before capturing it. |
| `--out PATH` | Output PNG path. If omitted, writes `screenshot.png` in the working directory. Use quotes if the path contains spaces. |

A selector is required unless you use `--list`. If there are no matches or multiple matches, the tool displays the relevant windows and exits without capturing; use a more specific selector.

## Capture size and behavior

- The image preserves the top edge, including the title bar when present, and crops **8 pixels from the left, right, and bottom**. It is not a client-area-only capture.
- For `--size 1280x720`, an outer window size of **1296 × 728 pixels** is requested to compensate for the crop. Without `--size`, the PNG is 16 pixels narrower and 8 pixels shorter than the outer window rectangle.
- If the window already has the requested outer dimensions, it is not resized or forced to repaint. When its size changes, a repaint is requested and the tool waits briefly before capturing.
- The window must be open. If minimized, it is restored before capture.
- Resizing affects the actual application and **is not undone** after capture.

## Limitations

- Some applications enforce minimum sizes or do not support resizing; in those cases, the PNG may not have the requested dimensions.
- The crop is fixed: it is not calculated individually for each window's theme, borders, or scaling.
- Windows with protected content, exclusive rendering, or certain hardware-accelerated rendering technologies may fail or produce incomplete captures with `PrintWindow`. Compatibility with every application is not guaranteed.
- The output directory must exist and be writable. An existing file at the output path may be overwritten.

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Window list displayed or capture saved. |
| `1` | GDI+ initialization failed. |
| `2` | Invalid arguments or missing selector. |
| `3` | No window matches the selector. |
| `4` | Failed to capture or save the PNG. |
| `5` | Multiple windows match the selector. |
