# TreeWalker

TreeWalker is an early-stage desktop file browser built with Qt Widgets and C++.
The current codebase provides a basic foundation for navigating folders, listing files, switching between detailed and thumbnail views, and experimenting with platform-specific filesystem integration.

This project is still in the initial development phase.
Only the basic framework is in place today, and the structure, features, and internal design are expected to change significantly over time.

## Current Overview

At the moment, TreeWalker includes:

- A Qt Widgets based desktop UI
- A folder tree and file list style navigation model
- Two file presentation modes: detailed list view and thumbnail view
- Basic keyboard-driven interaction for navigation and actions
- Hidden file toggling
- A small settings framework
- Filesystem provider abstraction for platform-specific backends
- Windows-specific shell integration hooks
- Experimental bookmark loading from Chrome bookmarks

## Project Structure

- `src/main.cpp`: application entry point, theme setup, and main window startup
- `src/MainWindow.*`: central UI flow and interaction logic
- `src/AbstractFileSystemProvider.*`: abstraction for filesystem access
- `src/BasicFileSystemProvider.*`: standard directory traversal implementation
- `src/WindowsFileSystemProvider.*`: Windows-specific filesystem integration
- `src/FetchLocationThread.*`: background loading for directory contents
- `src/FileItemModel.*`: item model used by list and thumbnail views
- `src/SettingsDialog.*` and related setting forms: application settings UI

## Build

TreeWalker currently uses qmake.

Typical requirements:

- Qt 6
- A C++17 compatible compiler
- qmake and the usual Qt development tools

Example build flow:

```bash
qmake TreeWalker.pro
make
```

Depending on the platform and local Qt setup, you may prefer an out-of-source build directory.

## Platform Notes

- Linux builds use the basic filesystem provider.
- Windows builds include additional shell-related functionality.
- Some paths and integrations are still hard-coded or experimental and may be revised later.

## Status

This repository should currently be treated as a prototype.
Expect incomplete features, rough edges, and large future refactors.

## Roadmap Direction

While the final direction is not fixed, the current code suggests interest in:

- Faster and smoother file navigation
- Richer file presentation and preview behavior
- Better platform integration
- Expanded settings and customization
- Additional providers or plugin-based extensions

## License

No license information has been added yet.
