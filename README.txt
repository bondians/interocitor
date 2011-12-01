Congratulations on your marriage!

Hope you like our little present.  We've been referring to it as "the interocitor" on #mokus-stay_out.  We'll have to add that term to the Bondi lexicon :)  The main work on this little project was done by Mark and Dan (with Maria's gracious approval and funding), but, in true Bondian fashion, almost the entire gang had a hand in its construction.  Your dad was a big help, he did much of the enclosure fabrication with Dave's help.  Ed provided the Arduino MCU board along with various bits of advice (in typical Ed-ish fashion), and even Uncle Larry lent a hand with the construction of the base Nixie clock kit (soldering).  Larry also graciously "donated" (after considerable arm-twisting on Dan's part) a piece of his ancient electrical arcana for the enclosure.  In it's former life, the metal enclosure was a high-power resistor "decade box" that had several blown-out resistors in it, but according to Larry "it was still good".  Consider yourselves blessed; I suppose in a more real sense you are.


Instructions for reprogramming the clock with new firmware - yes, this CAN be done on a Mac!

Step 1:
Get the AVR-CrossDev package from http://www.obdev.at/products/crosspack/index.html and install it.  Follow instructions on site to switch to avr-gcc version 4.

Step 2:
copy object code hex file (NixieClock.hex) to a known place in your file system; e.g. /Users/Shared/NixieClock/default/NixieClock.hex

Step 3:
Connect the clock to your computer (USB).  It will ennumerate as a serial port (tty).
To identify the clock's device name, search for it using (from terminal):
ls /dev/tty.*
look for a device name containing "usbserial"
for example, the device name on Guapa was /dev/tty.usbserial-A70075bH

Step 4:
invoke command from shell:
avrdude -pm328p -cstk500v1 -P /dev/tty.usbserial-A70075bH -b57600 -D -Uflash:w:/Users/Shared/NixieClock.hex:i
Note: Substitue correct path of file for "/Users/Shared/..."


Recompiling from source:

You will need the avr-gcc package.  It is recommened instead that you get the Objective Devleopment CrossPack for AVR at http://www.obdev.at/products/crosspack/index.html.  Note that you might need to add to your shell PATH a reference to /usr/local/CrossPack-AVR/bin BEFORE the reference in the PATH to /usr/local/bin.

At this time we do not have a makefile created to rebuild the project.  Mark is using the Windows-based "AVR Studio" IDE in conjunction with a external programmer board for development.  Unfortunately, even if you could run AVR Studio (e.g. with Paralysis :) this environment is not capable of using the built-in self-programming capability of the Arduino board.  However, avrdude, as mentioned above, does support the Arduino bootloader.  We will provide you with a makefile to rebuild the project and program the clock on a mac "real soon now".

The source code can be had from Ed's SVN repository at:

https://deepbondi.devguard.com/svn/bondiproj/trunk/NixieClock

You might have to talk to Ed to get the appropriate access credentials, I don't recall what they are :(  My SVN client remembers all this stuff for me :)


ADDENDUM:
We may just have a makefile for the project now.  Go to the directory where you have the project source saved, then switch to directory 'default' within the project directory and run 'make all'.
