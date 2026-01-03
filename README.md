# NoteSoFast

A lightweight, native Windows note-taking application built with Win32 API, SQLite, and Hunspell spell checking. NoteSoFast provides a responsive, efficient way to create and manage notes with real-time spell checking.

## Features

### Core Features
- **Create, Edit, Delete Notes**: Intuitive interface for managing text notes
- **Checklist Support**: Special checklist type for organizing subtasks
- **Tag Notes**: Organize and group notes or checklist using custom tags
- **Auto-Save**: Automatic persistence when switching notes or explicit save via toolbar
- **Search & Filter**: Quick search across note titles and content
- **Syntax Highlighting**: Color-coded note titles in the list view
- **Database Storage**: SQLite backend for reliable, persistent storage
- **Native UI**: Built with Win32 API for a responsive, lightweight experience

### Spell Checking
- **Real-Time Spell Checking**: Hunspell-powered spell checking with red underlines for misspelled words
- **Smart Word Detection**: Ignores incomplete words while typing, only checks complete words
- **Selection Awareness**: Pauses spell checking when text is selected to avoid interference
- **Multiple Dictionaries**: Support for various English locales (US, UK, AU, CA, ZA)

### Snippets
- **Text Expansion Shortcuts**: Define trigger → snippet pairs and expand them automatically when you type the trigger followed by a space.
- **Works in Notes + Checklists**: Enable expansion separately for standard notes and checklist input.
- **Cursor Placeholder**: Use `%%` inside a snippet to indicate where the caret should be placed after expansion.

Example:
- Trigger `;me` → Snippet `John William Hancock`
- Trigger `;uri` → Snippet `https://www.%%.com`

Configure snippets in **Settings → Snippets**. Triggers are case-sensitive.

## Building

### Prerequisites

- **C++ Compiler**: MSVC 2022 or later (Build Tools or full Visual Studio)
- **Windows SDK**: Included with Visual Studio or Build Tools
- **vcpkg**: For managing C++ dependencies
  - Install from: https://github.com/Microsoft/vcpkg

### Setup vcpkg

1. **Clone or download vcpkg**:
   ```cmd
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Set vcpkg root** (optional, adjust paths as needed):
   ```cmd
   set VCPKG_ROOT=C:\path\to\vcpkg
   ```

### Install Dependencies

Before building, install the required libraries using vcpkg:

```cmd
vcpkg install hunspell:x86-windows libiconv:x86-windows gettext-libintl:x86-windows
```

**Note**: The project is configured for 32-bit (x86-windows) builds. Adjust the triplet to `x64-windows` if building for 64-bit.

### Build with MSVC

1. **Open Developer Command Prompt for Visual Studio**:
   - Start Menu → search for "Developer Command Prompt"

2. **Navigate to project directory**:
   ```cmd
   cd C:\path\to\notesofast
   ```

3. **Run build script**:
   ```cmd
   build_msvc.bat
   ```
   
   Or manually with NMAKE (ensure you use the `.nmake` file):
   ```cmd
   nmake /f Makefile.nmake
   ```

The executable will be created in the `build\` directory along with required dictionary files and runtime DLLs.

### Build Configuration

The `Makefile.nmake` uses the following default paths:
- **vcpkg root**: `C:\Users\<username>\vcpkg` (configurable via VCPKG_ROOT variable)
- **Triplet**: `x86-windows` (32-bit, configurable via VCPKG_TRIPLET variable)

To use custom paths:
```cmd
nmake /f Makefile.nmake VCPKG_ROOT=C:\custom\vcpkg\path VCPKG_TRIPLET=x64-windows
```

## Running

The executable is created in the `build` directory:

```cmd
.\build\NoteSoFast.exe
```

Ensure the following are present in the `build` directory:
- `NoteSoFast.exe` - Main executable
- `hunspell-1.7-0.dll` - Spell checker library
- `intl-8.dll` - Internationalization library
- `iconv-2.dll` - Character encoding library
- `dict\en_US.aff` - English spell check rules
- `dict\en_US.dic` - English dictionary

These are automatically copied during the build process.

## Project Structure

```
notesofast/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── window.cpp/.h         # Main window and UI logic
│   ├── spell_checker.cpp/.h  # Hunspell integration
│   ├── database.cpp/.h       # SQLite database operations
│   ├── note.cpp/.h           # Note data structures
│   ├── utils.cpp/.h          # Utility functions
│   └── resource.rc           # Windows resource file
├── lib/
│   └── sqlite3.c/.h          # SQLite source
├── include/
│   └── sqlite3.h             # SQLite header
├── dict/en/                  # English spell check dictionaries
├── build/                    # Build output directory
├── Makefile.nmake            # NMAKE build configuration
├── build_msvc.bat            # Build script
└── README.md                 # This file
```

## Technology Stack

- **Language**: C++17
- **UI Framework**: Win32 API
- **Database**: SQLite 3
- **Spell Checking**: Hunspell 1.7
- **Build System**: NMAKE
- **Dependency Management**: vcpkg

## Troubleshooting

### Linker Errors (LNK1181)

If you encounter missing library errors:
```
LINK : fatal error LNK1181: cannot open input file 'libintl.lib'
```

Ensure all dependencies are installed via vcpkg:
```cmd
vcpkg install hunspell:x86-windows libiconv:x86-windows gettext-libintl:x86-windows
```

### DLL Not Found at Runtime

If the app fails to run with "DLL not found" errors:
1. Ensure all DLLs are present in the `build` directory (hunspell-1.7-0.dll, intl-8.dll, iconv-2.dll)
2. The build process automatically copies these; manually verify if needed:
   ```cmd
   copy "%VCPKG_ROOT%\installed\x86-windows\bin\hunspell-1.7-0.dll" build
   copy "%VCPKG_ROOT%\installed\x86-windows\bin\intl-8.dll" build
   copy "%VCPKG_ROOT%\installed\x86-windows\bin\iconv-2.dll" build
   ```

### Dictionary Files Not Found

Ensure spell check dictionary files are in `build\dict\`:
- `en_US.aff`
- `en_US.dic`

These should be automatically copied from `dict\en\` during build.

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.
