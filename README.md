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
- Asynchronous folder tree and file list loading, with up to four concurrent enumeration workers
- File and folder renaming with F2, including from thumbnail view
- Folder creation from the file list context menu, with name validation and conflict reporting
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
- `src/FetchLocationThread.*`: background loading for the folder tree and file list, with shared concurrency limits
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

The application requires both `IncrementalSearchPlugin` and `FileTypePlugin`, built separately with the same Qt version and build configuration. See [AGENTS.md](AGENTS.md) for their projects and static library prerequisites, and [tests/README.md](tests/README.md) for regression testing.

## File Operations and Loading

Press F2 while the folder tree, detailed list, or thumbnail view has focus to rename the selected filesystem item. Use **Create folder** in the detailed list context menu to add a folder to the current filesystem directory. Invalid names and existing destinations are rejected; failed operations report an error in the status bar.

Folder enumeration runs in the background. Tree children are inspected when expanded, and unchanged nodes retain their selection and expansion state. Navigation and refresh discard obsolete results; refresh preserves the file search and selected items. Tree and file list requests share a limit of four running workers, including interrupted requests whose underlying I/O has not returned. New requests wait for a slot when all four workers are occupied.

Hidden-file visibility uses filesystem metadata, including Windows hidden attributes, in both panes.

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
