Installation Instructions
==================

## Runtime Dependencies

- Postgres 9.5 (YOU WILL LOSE SYNC IF YOU RUN AGAINST 9.4!!!)

To install 9.5 while it's not official released, do this

- `wget --quiet -O - http://apt.postgresql.org/pub/repos/apt/ACCC4CF8.asc | sudo apt-key add -`
- `echo "deb http://apt.postgresql.org/pub/repos/apt/ trusty-pgdg main 9.5" >> /etc/apt/sources.list.d/postgresql.list`
- `apt-get update`
- `apt-get install postgresql-9.5`

Then proceed to configure the correct ports, databases, and roles.

## Build Dependencies

- `clang` >= 3.5 or `g++` >= 4.9
- `pkg-config`
- `bison` and `flex`


### Ubuntu 14.04

    # sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    # apt-get update
    # sudo apt-get install git build-essential pkg-config autoconf automake libtool bison flex libpq-dev clang++-3.5 gcc-4.9 g++-4.9 cpp-4.9


See [installing gcc 4.9 on ubuntu 14.04](http://askubuntu.com/questions/428198/getting-installing-gcc-g-4-9-on-ubuntu)

### OS X
When building on OSX, here's some dependencies you'll need:
Install xcode
Install homebrew
brew install libsodium
brew install libtool
brew install automake
brew install pkg-config

### Windows 
See [INSTALL-Windows.txt](INSTALL-Windows.txt)

## Basic Installation

- `git clone http://github.com/buhrmi/core`
- `cd core`
- `git submodule init`
- `git submodule update`
- `./autogen.sh`
- `CXX=clang++-3.5 ./configure`
- `make`
- `make check`
- `make install`
