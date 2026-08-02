# InstOS
Is a bit of a misnomer. 

It's not really in an "operating system" in the way a Linux distribution or RTOS is.

InstOS is simply a convenient (and attractive) name for a program that runs bare-metal on a Raspberry Pi CM4, that is both *instant* (~4 seconds to boot) and for *musical instruments*.

It was born out of [Jaffrack](https://github.com/joeljaffesd/Jaffrack), a project that utilizes two Raspberry Pi 4Bs talking over ethernet to create a distributed AV system, that served as my bass rig for many years. I prefer the development experience of bare-metal programming, so this project was born as an attempt to essentially remake Jaffrack in a form that doesn't require a full-blown operating system or the complexity of AlloLib.

## Basic Architecture 

InstOS is basically a wrapper around [Circle](https://github.com/rsta2/circle), a mature bare-metal Raspberry Pi framework. InstOS provides a build system / dev env that hides a lot of Circle's guts to enable focus on its specific use case. It's basically the build system for a particular Circle app, as well as some nice tools for flashing, debugging, etc.

## Getting Started

After cloning, run `init.sh` at the project root.

Then you can use `build.sh` to build, `flash.sh` to flash, and `monitor.sh` to monitor serial.

### Have fun.