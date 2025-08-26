# DM510FS – FUSE Filesystem with Caesar Cipher Encryption

This project implements a simple **user-level filesystem** using [FUSE (Filesystem in Userspace)](https://github.com/libfuse/libfuse).  
The filesystem stores files and directories in a custom in-memory structure backed by a binary save file, and it supports **Caesar cipher encryption/decryption** of file contents.

---

## 📂 Features
- **Basic filesystem operations**
  - Create/remove files (`mknod`, `unlink`)
  - Create/remove directories (`mkdir`, `rmdir`)
  - Read and write file contents (`read`, `write`)
  - Resize files (`truncate`)
  - Directory listing (`readdir`)
  - File attributes (`getattr`)
- **Persistence**
  - Filesystem state is saved to `saveFile.txt` when unmounted
  - Filesystem state is loaded from `saveFile.txt` on startup
- **Caesar Cipher**
  - File data is **encrypted on write** and **decrypted on read**
  - Shift value is provided as a command-line argument

---

## 🧩 Internal Design
- **Blocks**  
  - Each file uses fixed-size blocks (`BLOCK_SIZE = 8`)  
  - Up to `BLOCKS_COUNT = 10000` blocks available
- **Inodes**  
  - Up to `MAX_INODES = 4` entries  
  - Each inode tracks metadata (path, type, mode, timestamps) and up to `MAX_INODE_BLOCKS = 4` data blocks
- **Caesar cipher encryption**  
  - Applied per-block when writing  
  - Reversed when reading

---

## ▶️ Build & Run

### 🔨 Compile
You need the **FUSE development library** installed (e.g. `libfuse-dev` on Linux).

```bash
gcc -Wall dm510fs.c -o dm510fs `pkg-config fuse --cflags --libs`
