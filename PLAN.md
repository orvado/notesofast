# NoteSoFast - Development Plan

## Project Overview
A lightweight, native Windows note-taking application built with Win32 API and SQLite database backend. Focus on simplicity, performance, and minimal resource usage.

## Technical Stack
- **Language**: C/C++
- **GUI Framework**: Win32 API (native Windows controls)
- **Database**: SQLite 3
- **Build System**: MSVC or MinGW-w64
- **Target Platform**: Windows 10/11 (x64)

---

## Core Features (MVP - Phase 1)

### 1. Basic Note Management
- **Create, Edit, Delete Notes**
  - Plain text notes with title and content
  - Auto-save functionality (debounced)
  - Timestamp tracking (created, modified)
- **Note List View**
  - Scrollable list with note titles and preview
  - Sort by: date created, date modified, title (A-Z)
  - Double-click to open note

### 2. SQLite Database Schema
```sql
-- Notes table
CREATE TABLE notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    content TEXT,
    color_id INTEGER DEFAULT 0,
    is_archived INTEGER DEFAULT 0,
    is_pinned INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    modified_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Color categories table
CREATE TABLE colors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    hex_color TEXT NOT NULL
);

-- Checklist items table
CREATE TABLE checklist_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    note_id INTEGER NOT NULL,
    item_text TEXT NOT NULL,
    is_checked INTEGER DEFAULT 0,
    item_order INTEGER DEFAULT 0,
    FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE
);
```

### 3. User Interface Layout
- **Main Window** (Split view)
  - Left panel: Notes list (TreeView or ListBox)
  - Right panel: Note editor (Rich Edit control)
  - Top: Toolbar with common actions
  - Bottom: Status bar showing note count and last saved time
- **Menu Bar**
  - File: New, Save, Delete, Exit
  - Edit: Undo, Redo, Cut, Copy, Paste, Find
  - View: Sort options, Show/Hide archived
  - Help: About

### 4. Search Functionality
- **Quick Search Bar**
  - Real-time filtering of notes list
  - Search in title and content
  - Case-insensitive matching
  - Clear search button

---

## Enhanced Features (Phase 2)

### 5. Color-Coding System (Completed)
- **Predefined Color Categories**
  - Personal, Work, Ideas, Important, Shopping (5 default colors)
  - Visual indicator (colored bar/icon) in notes list
  - Filter notes by color category
  - Color picker dialog for customization (Implemented via Context Menu)

### 6. Checklist Support (Pending)
- **Toggle Note Type**: Text note ↔ Checklist
- **Checklist Features**
  - Add/remove checklist items
  - Check/uncheck items with visual strikethrough
  - Reorder items (drag-and-drop or up/down buttons)
  - Progress indicator (X/Y items completed)

### 7. Pin to Top (Completed)
- **Pinned Notes**
  - Pin/unpin button (pushpin icon)
  - Pinned notes always appear at top of list

### 8. Archive Functionality (Completed)
- **Archive Notes**
  - Archive/Unarchive button
  - Hide archived notes from main list by default
  - "Show Archived" toggle in toolbar

### 9. Sort Options (Completed)
- **Sort Criteria**
  - Date Modified (Default)
  - Date Created
  - Title (A-Z)
- **Sort Menu**
  - Accessible via Toolbar
  - Visual indicator for pinned status

### 8. Archive Functionality
- **Archive Notes**
  - Move notes to archive (hidden by default)
  - "Show Archived" toggle in View menu
  - Restore from archive option
  - Separate archived count in status bar

---

## Advanced Features (Phase 3)

### 9. Rich Text Formatting (Lightweight)
- **Basic Formatting** (using Rich Edit control capabilities)
  - Bold, Italic, Underline
  - Font size adjustment (3 preset sizes: Small, Normal, Large)
  - Bullet lists
  - Toolbar buttons for formatting

### 10. Auto-Link Detection
- **Automatic Hyperlink Recognition**
  - Detect URLs (http://, https://, www.)
  - Detect email addresses
  - Detect file paths (C:\...)
  - Click to open in default browser/email client/Explorer

### 11. Export/Import
- **Export Options**
  - Export single note to .txt file
  - Export all notes to folder (batch export)
  - HTML export (future consideration)
- **Import Options**
  - Import .txt files as new notes
  - Drag-and-drop .txt files to create notes

### 12. Keyboard Shortcuts
- `Ctrl+N`: New note
- `Ctrl+S`: Save note (explicit save)
- `Ctrl+F`: Focus search bar
- `Ctrl+D`: Delete current note
- `Ctrl+P`: Pin/unpin note
- `Ctrl+1-5`: Apply color category
- `F2`: Rename note (inline editing)

---

## Features to Defer/Exclude (Too Complex for Lightweight App)

### Not Implementing (To Keep App Lightweight)
- ❌ Calendar/Schedule integration (too complex)
- ❌ Reminder/notification system (requires background service)
- ❌ Sticky notes widgets (separate window management complexity)
- ❌ Password protection/encryption (security complexity)
- ❌ Online backup/sync (requires server infrastructure)
- ❌ Sharing via SMS/email (OS integration complexity)
- ❌ Web search integration (unnecessary feature creep)
- ❌ Map integration for addresses (heavy dependency)

---

## Implementation Roadmap

### Phase 1: Foundation (2-3 weeks)
1. Set up project structure and build system
2. Initialize SQLite database and create schema
3. Create main window with basic UI layout
4. Implement note list view (read from database)
5. Implement note editor (basic text editing)
6. Add Create/Edit/Delete functionality
7. Implement search functionality

### Phase 2: Core Enhancements (2 weeks)
1. Add color-coding system
2. Implement checklist support
3. Add pin to top feature
4. Implement archive functionality
5. Add sort options
6. Implement auto-save

### Phase 3: Polish & Advanced Features (2 weeks)
1. Add basic rich text formatting
2. Implement auto-link detection
3. Add export/import functionality
4. Implement keyboard shortcuts
5. Add settings dialog (preferences)
6. Testing and bug fixes

### Phase 4: Release Preparation (1 week)
1. Performance optimization
2. Memory leak testing
3. Create installer (optional: NSIS)
4. Write user documentation
5. Create README and license

---

## Technical Considerations

### Performance Goals
- **Startup time**: < 500ms
- **Database queries**: < 50ms for typical operations
- **Memory footprint**: < 50MB RAM
- **Executable size**: < 2MB (without SQLite DLL)

### Win32 Controls to Use
- `Edit` / `RICHEDIT50W`: Note editor
- `ListView`: Notes list with columns
- `Toolbar`: Action buttons
- `StatusBar`: Status information
- `ComboBox`: Color category selector
- `Dialog`: Settings, About, Color picker

### SQLite Best Practices
- Use prepared statements for all queries
- Enable WAL mode for better concurrency
- Create indexes on frequently queried fields (title, modified_at)
- Use transactions for batch operations
- Regular VACUUM for database optimization

### Code Organization
```
/notesofast
├── src/
│   ├── main.cpp              # Entry point, WinMain
│   ├── window.cpp/h          # Main window creation and message handling
│   ├── database.cpp/h        # SQLite wrapper and queries
│   ├── note.cpp/h            # Note data model
│   ├── ui_controls.cpp/h     # Custom control helpers
│   ├── utils.cpp/h           # Utility functions
│   └── resource.rc           # Resources (icons, menus, dialogs)
├── include/
│   └── sqlite3.h             # SQLite header
├── lib/
│   └── sqlite3.lib           # SQLite library
├── resources/
│   └── icons/                # Application icons
└── build/                    # Build output
```

---

## Success Criteria
- ✅ Fast and responsive UI
- ✅ Reliable data persistence
- ✅ Intuitive user experience
- ✅ Minimal dependencies (only SQLite)
- ✅ Small installation footprint
- ✅ Stable with no crashes or data loss

---

## Next Steps
1. Set up development environment (Visual Studio or MinGW)
2. Download SQLite amalgamation source
3. Create project structure
4. Begin Phase 1 implementation
