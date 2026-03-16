# C-Panic
---
## C Panic is a super minimal USBGuard clone for Windows

Core Futures as of NOW:
- Detecting changes regarding USB devices
- Config file for registering trusted devices
- Windows Toast notifications
- System tray icon for interaction
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

## ToDO: 
- Add application and system tray icon
- Find a way to register the program to open on startup
- Implement re-read of the whitelist file after editing is compleat (saved)
- Compiler optimization and security flags
- Try  C/C++ static analiysis ? (cppcheck)

### Building:
gcc .\main.c .\toast.c .\tray.c -municode -DUNICODE -D_UNICODE  -luser32 -lgdi32 -lsetupapi -lcfgmgr32 -Wextra
