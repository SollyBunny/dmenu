# dmenu - dynamic menu

dmenu is an efficient dynamic menu for X.

This repo is a fork of dmenu made for myself

## Patches

The code has been somewhat mangled and taking a diff with the original will not give you anything useful. Here are the patches which were applied, they all have merge conflicts and have to be edited / rewritten to work at all.

* [https://tools.suckless.org/dmenu/patches/alpha/dmenu-alpha-20230110-5.2.diff](https://tools.suckless.org/dmenu/patches/alpha/dmenu-alpha-20230110-5.2.diff)
* [https://tools.suckless.org/dmenu/patches/center/dmenu-center-5.2.diff](https://tools.suckless.org/dmenu/patches/center/dmenu-center-5.2.diff)
* [https://tools.suckless.org/dmenu/patches/fuzzyhighlight/dmenu-fuzzyhighlight-caseinsensitive-4.9.diff](https://tools.suckless.org/dmenu/patches/fuzzyhighlight/dmenu-fuzzyhighlight-caseinsensitive-4.9.diff)
* [https://tools.suckless.org/dmenu/patches/fuzzymatch/dmenu-fuzzymatch-4.9.diff](https://tools.suckless.org/dmenu/patches/fuzzymatch/dmenu-fuzzymatch-4.9.diff)
* [https://tools.suckless.org/dmenu/patches/grid/dmenu-grid-4.9.diff](https://tools.suckless.org/dmenu/patches/grid/dmenu-grid-4.9.diff)
* [https://tools.suckless.org/dmenu/patches/gridnav/dmenu-gridnav-5.2.diff](https://tools.suckless.org/dmenu/patches/gridnav/dmenu-gridnav-5.2.diff)
* [https://tools.suckless.org/dmenu/patches/numbers/dmenu-numbers-20220512-28fb3e2.diff](https://tools.suckless.org/dmenu/patches/numbers/dmenu-numbers-20220512-28fb3e2.diff)

## Requirements
* XLib
* Xft


## Installation

Compile configuration is in `config.mk`, the default install directory is `/usr/local`

You can compile and install with
```sh
make clean install
```

## Running

```sh
dmenu_run
```

More advanced usage can be found in man page

## Configuration

Configuration can be found in `config.h`, a default config can be found in `config.def.h`
