# Diary for AmigaOS 3.2

A simple one-page-per-day journal for 68k Amigas running AmigaOS 3.2.
It's the Amiga sibling of [Diary for the classic Mac OS](https://github.com/retrolemmy/Diary)
(on 68k Macs and on PowerPC Macs up to a G4 with Mac OS 9.2.2),
[Diary for Mac OS X](https://github.com/retrolemmy/Diary-MacOSX) and
[Diary for Windows 98](https://github.com/retrolemmy/Diary-Win98).

- One window: a month calendar and a list of all entries (newest first) on
  the left, the selected day on the right with `<`, `Today` and `>` buttons
- In the calendar, days with an entry are **bold**, today is framed and the
  open day is highlighted; click the arrows to switch months. Click a day
  (or a list entry) to open it
- **Find** (Amiga-F) searches every entry (ignoring case) and narrows the list
  to the matches; **Find Again** (Amiga-G) jumps to the next match, across
  entries; **Show All Entries** clears the search
- **Go** menu: Previous Day (Amiga-[), Next Day (Amiga-]), Today (Amiga-T)
- **Settings → Entry Font...** picks the entry text's font with the standard
  font requester; the choice is remembered in a "Diary Settings" file in the
  entries drawer
- The current entry is saved automatically when you switch days or quit;
  Amiga-S saves at any time
- Entries are plain text files named `YYYY-MM-DD` in a **Diary Entries**
  drawer next to the program, so any text editor can open them
- Entries written by the Mac version can be copied in and read as they are:
  their line ends and accented letters (MacRoman) are converted, and the Mac
  version reads the entries written here the same way
- Clearing an entry completely deletes its file
- **Settings → Diary Server...** keeps the entries on a
  [Diary Server](https://github.com/retrolemmy/Diary-Server) instead, so all
  your Amigas and Macs share them. Search then runs on the server, and if an
  entry was changed on another computer since you opened it, Diary asks
  before saving over it. It needs a TCP/IP stack such as Roadshow; without a
  server set, Diary never touches the network

Diary uses the ReAction classes that come with AmigaOS 3.2 (including
`texteditor.gadget`), so it needs 3.2 or later; on older systems it tells
you which classes are missing.

## Diary Server

To share the entries between your computers, run
[Diary Server](https://github.com/retrolemmy/Diary-Server) on a machine in
your network, such as a RetroNAS or any small Linux box with Python 3.9 or
later: download `diary_server.py` from its
[latest release](https://github.com/retrolemmy/Diary-Server/releases/latest)
and start it as its README describes. Diary works with Diary Server 1.0 or
later. The same server also gives modern browsers a Diary web page.

## Building

You need [bebbo's amiga-gcc](https://codeberg.org/bebbo/amiga-gcc) with the
NDK 3.2:

```sh
git clone https://codeberg.org/bebbo/amiga-gcc.git
cd amiga-gcc
echo "$HOME/Projects/amiga-toolchain" > .prefix
make update NDK=3.2 && make all -j16 NDK=3.2
```

(The build may end with an error in the optional libSDL12; that doesn't
matter for Diary.) Then build Diary:

```sh
make                # dist/Diary and dist/Diary.info
make install        # copies them to ~/Amiberry/HardDrives/Shared/Diary
```

Set `TOOLCHAIN=` or `INSTALL_DIR=` on the `make` command line if yours are
elsewhere.

## Icon

`Diary.info` is drawn by `tools/make_icon.py` in the four standard
Workbench colours.

## License

No rights reserved: Diary is dedicated to the public domain under
[CC0 1.0](LICENSE). Use it for anything, no attribution needed.
