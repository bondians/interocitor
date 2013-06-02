Building the interocitor firmware
=================================

This project uses AVR Crosspack and a 'shake' build system.

Before building
---------------

Install AVR Crosspack and Haskell Platform.  Make sure both are in your PATH.  Install the `shake` and `avr-shake` packages using `cabal`:

    cabal install shake avr-shake

Building
--------

To build:

    ./shake

To upload (may require customization of build tool, described below):

    ./shake flash

To clean:

    ./shake clean

See also:

    ./shake --help

Customizing the build
---------------------

Just edit ./shake, it's a Haskell source file.  Like a typical makefile, it defines a bunch of flags and such at the top.
