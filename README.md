# SM3DW Mod Installer NX

A Nintendo Switch homebrew application that allows you to browse and install **GameBanana mods for Super Mario 3D World** directly from your console.

This app connects to the official GameBanana API, lets you browse categories, view mod information, download ZIP archives, and automatically install detected `romfs` and `exefs` folders into Atmosphère's layeredFS directory.

---

## Features

- Browse GameBanana mod categories
- View mod metadata:
  - Author
  - Version
  - Like count
  - View count
- Scrollable mod list (no screen overflow)
- Page navigation (L/R)
- View mod descriptions
- Download mod files directly from GameBanana
- Automatic ZIP extraction
- Search feature
- Automatic detection of:
  - `romfs`
  - `exefs`
- Installs to:

sdmc:/atmosphere/contents/010028600EBDA000/


- Safe extraction (prevents absolute paths and directory traversal)
- Clean console-based UI

---

## Controls

| Button | Action |
|--------|--------|
| D-Pad Up / Down | Move selection |
| A | Open mod / Download file |
| B | Go back |
| L / R | Change page |
| X | Refresh |
| + | Exit |
| Y | Search/Clear search |

---

## Download (Prebuilt Version)

If you do not want to build from source:

1. Go to the **Releases** tab on this repository.
2. Download the latest `.nro` file.
3. Copy it to:

sdmc:/switch/


4. Launch it from the Homebrew Menu.

---

## Building From Source

### Requirements

- devkitPro
- libnx
- curl (libcurl)
- jsmn
- miniz
- Nintendo Switch with Atmosphère CFW
- Internet connection (on your PC and Nintendo Switch)

### Setup

1. Install devkitPro from:  
   https://devkitpro.org/

2. Ensure `DEVKITPRO` environment variable is set.

3. Paste this into the command prompt to build:

```bash
git clone https://github.com/Jack-The-Yoshi/SM3DW-Mod-Installer-NX.git
cd SM3DW-Mod-Installer-NX
make
```

The compiled .nro will be located in the project folder.

Copy it to:

sdmc:/switch/

### How It Works
The app uses the GameBanana API v11 to:

Fetch mod listings

Fetch metadata (author, version, likes, views)

Fetch file download URLs

Fetch descriptions

When a mod is downloaded:

The ZIP archive is extracted using miniz.

The app scans recursively for:

romfs

exefs

Found folders are copied into Atmosphère's layeredFS structure.

Currently supported archive type:

ZIP only

RAR support is planned.

### Important Disclaimer
This application does not host any mods.

All mod files are downloaded directly from GameBanana.

I do not own, create, or take credit for any mods downloaded through this tool.

All credit belongs to the original mod authors.

This tool installs files directly to your SD card at:

sdmc:/atmosphere/contents/010028600EBDA000/
Installing mods always carries risk.

You are responsible for any damage caused to your SD card or game files.

Always keep a backup of your SD card before installing mods.

Use at your own risk.

This project is not affiliated with Nintendo, GameBanana, or the Super Mario franchise.

### Known Limitations
ZIP archives only (RAR not yet supported)

Console-based UI only

### Planned Features
RAR support

Sorting (Most liked / Most viewed)

Improved download progress UI

Favorites system

### Author
Created by Jack The Yoshi

### Credits
GameBanana API

devkitPro

libnx

curl

jsmn

miniz

Atmosphère

All the amazing people who are making Super Mario 3D World mods!

### Screenshots

![2026021715504400-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/d1dbba4a-17f3-404b-a15b-ec34bec52258)

![2026021715484800-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/0fc1f481-9eeb-44a3-a831-e2eb0d2ba90f)

![2026021715434900-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/6ea900a3-0b90-4743-9b28-580de10aa109)

![2026021715431700-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/b439c358-dc3e-4cc9-a029-5bc2ac439e37)

![2026021715431400-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/08518637-c93f-4fd1-9f37-82594f63cea5)

![2026021715430801-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/e3448845-dc5b-4de8-959e-741d35a5c6de)

![2026021715424700-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/3945e01f-8984-4068-b704-2e5c917a3fb2)

![2026021715290100-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/c7e63773-409e-4fd8-85b7-c3174aa31fcc)

### Note
Your Nintendo Switch needs an Internet connection for this app to work.
