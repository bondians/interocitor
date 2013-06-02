#!/usr/bin/env runhaskell
{-# OPTIONS_GHC -fwarn-unused-imports -fwarn-unused-binds #-}
-- |Build system for interocitor.  Requires shake and avr-shake packages:
--
-- > cabal install shake avr-shake
module Main (main) where

import Control.Monad
import Development.Shake
import Development.Shake.AVR
import Development.Shake.FilePath

project         = "NixieClock"
mcu             = "atmega328p"
usbPort         = "/dev/tty.usbserial-A70075bH"
avrdudeFlags    = ["-pm328p", "-cstk500v1", "-b57600", "-D"]

srcDir          = "src"
buildDir        = "build" -- NB: "clean" will blast this dir.

mmcuFlag        = "-mmcu=" ++ mcu
cppFlags        = ["-DF_CPU=16000000UL", "-Iinclude"]
cFlags          = cppFlags ++ 
    [mmcuFlag, "-Wall", "-gdwarf-2", "-std=gnu99",
     "-Os", "-funsigned-char", "-funsigned-bitfields",
     "-fpack-struct", "-fshort-enums"]

ldFlags         = [mmcuFlag, "-Wl,-Map=" ++ buildDir </> project <.> "map"]

hexFlashFlags   = ["-R", ".eeprom", "-R", ".fuse", "-R", ".lock", "-R", ".signature"]
hexEepromFlags  = ["-j", ".eeprom"]

main = shakeArgs shakeOptions $ do
    want [project <.> "hex", project <.> "eep", project <.> "lss"]
    
    phony "clean" $ do
        removeFilesAfter "." ["*.hex", "*.eep", "*.lss"]
        buildExists <- doesDirectoryExist buildDir
        when buildExists $ removeFilesAfter buildDir ["//*"]
    
    phony "flash" $ do
        avrdude avrdudeFlags (project <.> "hex") usbPort
    
    "*.hex" *> withSource (buildFile "elf") (avr_objcopy "ihex" hexFlashFlags)
    "*.eep" *> withSource (buildFile "elf") (avr_objcopy "ihex" hexEepromFlags)
    "*.lss" *> withSource (buildFile "elf") avr_objdump
    
    buildDir </> "*.o" *> withSource (srcFile "c") (avr_gcc cFlags)
    
    buildDir </> project <.> "elf" *> \out -> do
        srcs <- getDirectoryFiles srcDir ["*.c"]
        let objs = map (\src -> buildDir </> replaceExtension src "o") srcs
        avr_ld' "avr-gcc" ldFlags objs out

-- A few utility functions used above
withSource f action out = action (f out) out
srcFile   ext = flip replaceExtension ext . flip replaceDirectory srcDir
buildFile ext = flip replaceExtension ext . flip replaceDirectory buildDir
