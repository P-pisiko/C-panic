# C-Panic
---
## C Panic is a super minimal USBGuard clone for Windows

Core Futures as of NOW:
- Detecting changes regarding USB devices
- Config file for registering trusted devices
- Windows Toast notifications
- System tray icon for interaction [Coming Soon]
- Enumurating available USB devices to get information

## Usage:
Supply -h or --help to get help
```
C Panic - USBGuard 'Clone' for windows

--help, -h      Show this help message
--edit, -e      Open the whitelist file for editing
--list, -l      List currently plugged USB devices
```
---

## Note: 
if you double click to run the application you need to use Task Manager to close it. This will be fixed when system tray icon is implemented.

### Building:
gcc .\main.c .\toast.c -municode -DUNICODE -D_UNICODE  -luser32 -lgdi32 -lsetupapi -lcfgmgr32 -Wextra
