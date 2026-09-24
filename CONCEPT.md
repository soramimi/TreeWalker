# TreeWalker

TreeWalker is a personal file manager inspired by the simplicity and directness of Windows Explorer from the Windows 95–2000 era.

Modern file managers have accumulated many abstractions, special views, virtual folders, category-based navigation items, and UI layers that can make direct filesystem navigation feel less predictable. TreeWalker takes the opposite approach:

> The filesystem should look like a filesystem.

TreeWalker is designed for people who want to move through directories and files quickly, explicitly, and with as little shell-specific abstraction as possible.

## Concept

The core design goal is to provide a file manager that feels closer to classic Windows Explorer:

- a directory tree on the left
- the contents of the current directory on the right
- real filesystem paths
- explicit file operations
- predictable keyboard-oriented navigation
- minimal special treatment of files and folders

TreeWalker intentionally avoids many concepts that modern file managers place at the top level.

Items such as:

- Documents
- Gallery
- Music
- Pictures
- Videos

do not need to be promoted as primary navigation concepts merely because the operating system considers them special.

If they are directories, they can be shown as directories.

If they are not useful, they do not need to be shown at all.

Home and Desktop may still be useful shortcuts, but the navigation tree should primarily reflect locations that the user actually wants to navigate.

## Files are files. Directories are directories.

TreeWalker does not treat archive files as virtual folders.

A ZIP file is a file.

It should look like a file, behave like a file, and remain distinguishable from a real directory.

Archive operations such as viewing contents or extracting files should be explicit operations rather than transparent filesystem-like navigation.

In particular, TreeWalker does not aim to reproduce the behavior of Windows' compressed-folder shell extension (`zipfldr.dll`).

The distinction is intentional:

- directory → navigable filesystem object
- archive → ordinary file with archive-specific operations

This keeps the visual and conceptual model consistent with the actual filesystem.


## File extensions are always visible

TreeWalker always displays filename extensions.

Hiding extensions for registered file types is considered actively harmful because it obscures the real filename, weakens the visual distinction between file types, and can make similarly named files harder to reason about safely.

For example:

```text
report.txt
report.exe
report.pdf
```

should always remain visibly distinct as complete filenames.

TreeWalker therefore follows two rules:

- file extensions are never hidden
- the extension is visually emphasized in **bold**

The purpose of bolding the extension is not decoration. It makes the file type immediately scannable while preserving the full filename as a single textual unit.

This is especially important when filenames share the same stem or when a misleading icon, file association, or executable masquerading as another file type could otherwise reduce clarity.

TreeWalker treats the extension as part of the filename, not as optional metadata.

## Item view modes

The right pane intentionally provides only two primary view modes.

### Details view

For working with files as structured data.

Typical columns include:

- Name
- Size
- Type
- Modified time

This is the main mode for sorting, comparing, selecting, renaming, moving, and otherwise managing files.

### Thumbnail view

For files whose contents are easier to recognize visually, such as:

- images
- videos
- PDFs
- other previewable formats

Traditional large-icon / medium-icon / small-icon modes are intentionally omitted.

They occupy an awkward middle ground between information-dense list views and content-oriented thumbnail views, while adding additional UI modes and configuration that TreeWalker does not need.

## Japanese incremental search

One of TreeWalker's main features is incremental search for Japanese filenames using romanized input.

For example, a filename such as:

```text
東京都写真整理メモ.txt
```

should be searchable without switching the IME on and typing Japanese directly.

Typing something like:

```text
toukyou
```

or:

```text
shashin
```

should narrow the item list or move the selection toward matching files immediately.

The goal is similar in spirit to Migemo, but TreeWalker does not rely on Migemo-style query expansion.

### Why not Migemo?

A Migemo-based prototype was tested first.

Migemo works by expanding romanized input into a broad Japanese search expression. This is useful for general text search, but for filename navigation it can produce more matches than desired.

For TreeWalker, higher precision is more important than broad text-search recall.

### MeCab-based reading generation

TreeWalker instead uses MeCab to analyze Japanese filenames and obtain their readings.

Conceptually:

```text
東京都写真整理メモ
        ↓
形態素解析
        ↓
東京 / 都 / 写真 / 整理 / メモ
        ↓
トウキョウ / ト / シャシン / セイリ / メモ
        ↓
toukyou / to / shashin / seiri / memo
```

The original filename and generated reading forms can then be searched in parallel.

This makes it possible to match a file by:

- its original Japanese filename
- kana reading
- romanized reading

For example:

```text
東京
とうきょう
toukyou
```

can all refer to the same filename.

The reading information can be generated when directory entries are loaded and cached as search metadata, allowing incremental search itself to remain lightweight.

## Incremental navigation, not just search

The intended interaction is closer to classic filename selection than to opening a separate search interface.

When the item view has focus, typing characters should immediately begin narrowing or selecting matching entries.

For example:

```text
t
to
tou
touk
touky
toukyou
```

should progressively refine the match.

The user should not need to:

1. open a search box
2. enable the Japanese IME
3. type the filename in Japanese
4. submit the query
5. leave search mode again

The feature is meant to make Japanese filenames as easy to navigate from the keyboard as ASCII filenames.

## Design principles

TreeWalker's design can be summarized by a few rules.

### Show the filesystem directly

Do not replace the filesystem hierarchy with semantic categories unless there is a clear benefit.

### Avoid unnecessary virtual folders

Special shell namespaces should not be allowed to blur the difference between real directories and other objects.

### Keep object types visually honest

A ZIP archive should not look like a directory.

A directory should not behave like a document category.

### Prefer a small number of strong UI modes

Two useful item views are better than many overlapping presentation modes.

### Optimize for keyboard navigation

TreeWalker should make moving through large directory trees fast and predictable.

### Treat Japanese filenames as first-class input

Romanized incremental search is not an optional convenience. It is part of the core navigation model.

## What TreeWalker is not

TreeWalker is not intended to reproduce every feature of Windows Explorer or Finder.

It is not primarily:

- a media library
- a photo gallery
- a document organizer
- a shell namespace browser
- a cloud storage dashboard
- an archive manager
- a universal content launcher

Those functions may be handled by dedicated applications.

TreeWalker focuses on one task:

> navigating and managing files and directories efficiently.

## Motivation

Windows Explorer from the Windows 95–2000 era provided a straightforward model:

- directory hierarchy on the left
- current directory on the right
- minimal indirection between the UI and the filesystem

Later versions gradually introduced more shell abstractions, special folders, virtual locations, content-oriented categories, and additional view modes.

TreeWalker is an attempt to recover the parts of the older model that still work well, while adding features that are especially useful today — most notably fast Japanese filename navigation.

The goal is not nostalgia for an old UI.

The goal is a file manager whose behavior remains simple enough to form muscle memory.
