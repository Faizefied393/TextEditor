# TextEditorProject (Kilo)

This project is a minimal, terminal-based text editor written in C. It was developed by following the Kilo tutorial by Snaptoken:
https://viewsourcecode.org/snaptoken/kilo/

The purpose of this project is to gain hands-on experience with low-level systems programming concepts, including raw terminal input, escape sequence handling, screen rendering, file I/O, and basic text editing.

---

## Table of Contents
1. Introduction  
2. Features  
3. Build & Run  
4. Usage  
5. Controls  
6. Project Structure  
7. License  

---

## Introduction
Kilo is a small educational text editor that runs entirely inside a terminal no graphical interface. This implementation closely follows the Kilo tutorial and demonstrates how a functional text editor can be built from scratch in C using system calls and terminal control.

This project was completed as part of a coursework assignment and emphasizes correctness, functionality, and incremental development using Git.

---

## Features
- Open and view text files
- Cursor navigation using arrow keys
- Insert and delete text
- Create new lines
- Save files to disk
- Status bar and message bar
- Search functionality with incremental updates (if implemented)
- Syntax highlighting for supported file types (if implemented)

---

## Installation

To compile and run the Kilo text editor, you need a C compiler and a terminal environment.


### Prerequisites
- A C compiler (GCC or Clang)
- A Unix-like environment (Linux, macOS, or WSL)

### Steps

1. Clone the repository to your local machine:
    ```bash
    git clone https://github.com/inserturuser/kilo-text-editor.git
    ```

2. Navigate to the project directory:
    ```bash
    cd urdirectoryname

3. Compile the project using GCC or any C compiler:
    ```bash
    gcc -o kilo kilo.c
    ```

4. Run the editor:
    ```bash
    ./kilo
    ```
## Usage

Once the editor is compiled, you can use it to open and edit text files from your terminal.