# Scroll with Pointer Movement CLI

Scroll without a scroll wheel by moving a pointer (mouse, trackball, touchpad, trackpoint). Enables scrolling with devices without a scroll wheel (e.g. Logitech Marble Trackball). General ergnonomics utility. Command line tool.

Activation (see -s option):
- hold keyboard key (combo)
- toggle with keyboard key (combo) on/off

# Dependencies
- Linux
- X11
- libXi
- libXtst
- libXfixes

## Install Dependencies

### Debian-based system (Debian, Ubuntu):
`sudo apt install libxi6 libxtst6 libxfixes3`

### Fedora / Redhat:
`sudo dnf install libXi libXtst libXfixes`

# Help
- exec with option -h to see the options
- -s option is the shortcut key code. You need to set this, for it to work, although it starts without it.
  - to find the (xorg) key code to be used with the -s option: start with -d, then press a button. The debug output will print the key code that can be used with -s option.