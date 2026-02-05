# TextEditor (Kilo)

A minimal, terminal-based text editor written in C.  
This project is an educational implementation inspired by the *Kilo* text editor guide by Snaptoken, designed to demonstrate low-level text editing concepts such as raw terminal input, screen rendering, file I/O, searching, and syntax highlighting.

---

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Build Instructions](#build-instructions)
5. [Usage](#usage)
6. [Controls](#controls)
7. [Project Structure](#project-structure)
8. [Notes](#notes)
9. [License](#license)

---

## Overview

This project implements a fully functional terminal text editor using standard C and POSIX system calls.  
The editor runs directly in the terminal using raw mode input and ANSI escape sequences, without relying on external libraries or graphical interfaces.

The goal of this project is to understand:
- How terminal applications handle input/output
- How text editors manage buffers and cursor movement
- How file reading, writing, and searching work at a low level

---

## Features

- Open and edit text files from the terminal
- Real-time keyboard input handling raw mode
- Cursor movement with arrow keys
- Insert and delete characters
- Create and edit multiple lines
- Save files to disk
- Search within files `Ctrl-F`
- Syntax highlighting for C source files
- Status bar and message bar
- Graceful exit with unsaved-change warnings

---

## Requirements

- Linux or Unix-like environment Linux, macOS, or WSL
- GCC compiler
- GNU Make

---

## Build Instructions

Clone the repository:

```bash
git clone https://github.com/Faizefied393/TextEditor
cd TextEditor

make

```
### Usage
```
Run the editor

./kilo

Open a file

./kilo filename.txt

```
### Controls

| Key                 | Action                 |
| ------------------- | ---------------------- |
| Arrow Keys          | Move cursor            |
| Enter               | Insert new line        |
| Backspace           | Delete character       |
| Ctrl-S              | Save file              |
| Ctrl-Q              | Quit editor            |
| Ctrl-F              | Search within file     |
| Home / End          | Jump to line start/end |
| Page Up / Page Down | Scroll                 |

### Project Structure

TextEditor/
 kilo.c        # Main editor implementation single-file
 Makefile      # Build system
 .gitignore    # Ignored build artifacts
 README.md     # Project documentation

### Notes
The editor is implemented as a single C source file for clarity and learning purposes.

Terminal control is handled using termios, ioctl, and ANSI escape sequences.

Memory management is handled manually using malloc, realloc, and free.

This project prioritizes learning and correctness over performance or extensibility.
