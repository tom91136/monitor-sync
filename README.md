# monitor-sync

A small utility that synchronises X11 DPMS state across multiple machines on the local network.

## Prerequisites

* X11/Xext headers - `libx11-dev libxext-dev` on Ubuntu/Debian
* CMake 3.16+
* Any C++17 capable compiler

## Building

```shell
cd monitor-sync

cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release  # generate a build
cmake --build build -j                        # compile
 
./build/monitor-sync --help 
```

## Installing

This program requires a working X11 server already running so the built-in installation target makes
use of XDG Autostart. Essentially, .desktop files will be executed at session start when placed
in `~/.config/autostart`.

```shell
# Omitting -DCMAKE_INSTALL_PREFIX will default to /usr/local/bin which requires elevated privilege at install-time
# Specify -DAUTOSTART_DIR for a different XDG autostart path
cmake -Bbuild -H. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local/ # install to ~./local/bin

# proceed to install the binary itself
# install monitor-sync to ${CMAKE_INSTALL_PREFIX}
cmake --build build --target install 

# on the server machine
cmake --build build --target install-autostart-server # install the .desktop file to AUTOSTART_DIR
 
# on the client machine
cmake --build build --target install-autostart-client # install the .desktop file to AUTOSTART_DIR
```

## Quick start

On the host machine (the machine that will be broadcasting the DPMS state from):

```shell
monitor-sync server
```

On another machine in the same LAN:

```shell
monitor-sync client
```

The DPMS state of the client should now be synchronised to the host.



For more options, consult the CLI help:

```shell
> monitor-sync --help
  monitor-sync COMMAND {OPTIONS}

    monitor-sync

  OPTIONS:

      arguments
        -h, --help                        help
        commands
          server                            Start the server, use this on the machine you are monitoring DPMS of
            -r[RATE], --rate=[RATE]           The rate in hertz at which X11's DPMS state is polled
                                              Default: 1
          client                            Start the client, use this on machines that needs to have the same DPMS state as the server
            -i[IP], --ip=[IP]                 The *multicast* (i.e starts with 224.*.*.*, this is NOT the your normal IP address) address, defaults to broadcast if not specified
        -d[DISPLAY], --display=[DISPLAY]  The X display to use (e.g `:0`), omit for the default display
        -p[PORT], --port=[PORT]           The UDP port to use for sending/receiving sync messages
                                          Default: 3000

    Client/server for syncing DPMS (monitor power) states

```