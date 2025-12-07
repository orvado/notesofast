# NoteSoFast

A lightweight, native Windows note-taking application built with Win32 API and SQLite.

## Features (Phase 1)

- **Create, Edit, Delete Notes**: Basic text notes.
- **Auto-Save**: Notes are saved automatically when switching or explicitly via toolbar.
- **Search**: Filter notes by title or content.
- **Database**: SQLite backend for reliable storage.
- **UI**: Split view with list and editor.

## Building

### Prerequisites

- **C++ Compiler**: MinGW-w64 (g++) or Visual Studio (MSVC).
- **Windows SDK**: For Win32 API headers/libs.

### Build with MinGW

```powershell
mingw32-make
```

### Build with MSVC

Open "Developer Command Prompt for VS" and run:

```cmd
build_msvc.bat
```

## Running

The executable is created in the `build` directory.

```powershell
.\build\NoteSoFast.exe
```
