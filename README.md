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
- File opening in the default application after an OK / Cancel confirmation
- Hidden file toggling
- Asynchronous folder tree and file list loading, with up to four concurrent enumeration workers
- File and folder copying and moving with Ctrl+C / Ctrl+X / Ctrl+V in all three views
- Drag-and-drop transfers with a mandatory Move / Copy / Cancel prompt
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

## Source Tree and Generated Files

Build outputs, Autotools caches, configure results and Libtool executable wrappers are not versioned. MeCab retains its distribution `configure`, `Makefile.in`, macro/support files and Windows `Makefile.msvc`, so its documented direct configure and Windows builds remain possible. Oniguruma's `autogen.sh` and the plugin's `build-gcc.sh` regenerate its Autotools support scripts and generic `INSTALL` guide with `autoreconf -if`; Autoconf, Automake and Libtool are required for that workflow.

Handwritten Makefiles, platform-specific configuration headers, dictionary source data and embedded dictionary C arrays remain build inputs. The raw `gen/magic.mgc` and `gen/magic.mgc.zst` copies duplicate the embedded C array and are omitted; the dictionary generation script produces working outputs in the ignored `lib/` directory. Generate dependencies before building the application and plugins as described above.


## File Operations and Loading

In the detailed list and thumbnail view, press Enter or double-click a file to request opening it in its default application. Each operation first shows the filename and full path with **OK / Cancel**, defaulting to Cancel. Esc and closing the dialog also cancel. Enter opens only the current item; double-click opens only the clicked item, even with multiple items selected. Search and selection are preserved, and the saved file path is checked again after OK. Failed launch requests are reported in the status bar. Folder navigation and bookmark URL activation retain their existing behavior.

Press F2 while the folder tree, detailed list, or thumbnail view has focus to rename the selected filesystem item. Use **Create folder** in the detailed list context menu to add a folder to the current filesystem directory. Invalid names and existing destinations are rejected; failed operations report an error in the status bar.

Ctrl+C and Ctrl+X put the selected filesystem items on the clipboard. Ctrl+V pastes into the selected tree folder or the directory displayed in the detailed/thumbnail view. Cutting does not change files until paste succeeds. Text editors retain their normal clipboard shortcuts.

Dragging onto a folder transfers into that folder; dropping on the detailed/thumbnail background transfers into the displayed directory. Tree background and ordinary file rows reject drops. Every valid drop asks **Move / Copy / Cancel**, with Cancel as the default. Filesystem location and Ctrl/Shift never automatically select an operation. Unsupported actions offered by an external drag source are refused after confirmation, rather than substituted with another operation.

Transfers run in a separate background job, one at a time, with progress and cancellation. Existing destinations require Skip, Different name, Replace, or Cancel; replacing a folder replaces the whole folder rather than merging it. Completed items remain after cancellation or partial failure. Failures report recovery paths if temporary output or the original destination backup could not be removed. Source/destination changes detected during copying stop the affected operation.

Standard file URL clipboard formats support external file managers. Incoming external drops are supported when the source permits the selected operation and a copy acknowledgment, so only TreeWalker handles source deletion. Outgoing drags expose an application-private format and are intentionally limited to TreeWalker: external targets must not transfer files without TreeWalker's mandatory confirmation. Linux interoperability has been tested with Thunar; Explorer integration still needs verification on Windows.

Copies preserve contents, links, permissions and modification times where supported; Windows copies also preserve common file attributes. Full ACLs, ownership, extended attributes and sparse layout are not guaranteed. Unsupported special files and non-symbolic reparse points are rejected. Source links are copied as links and are not recursively followed.

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
