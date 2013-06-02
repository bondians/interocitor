#!/usr/bin/env runhaskell
{-# OPTIONS_GHC -fwarn-unused-imports -fwarn-unused-binds #-}
module Main (main) where

import Control.Monad
import Development.Shake
import Development.Shake.AVR
import Development.Shake.FilePath

srcDir          = "src"
buildDir        = "build" -- NB: "clean" will blast this dir.

usbPort         = "/dev/tty.usbserial-A70075bH"

project         = "NixieClock"
mcu             = "atmega328p"

common          = ["-mmcu=" ++ mcu]

cppFlags        = common ++ ["-DF_CPU=16000000UL", "-Iinclude"]
cFlags          = cppFlags ++ 
    ["-Wall", "-gdwarf-2", "-std=gnu99",
     "-Os", "-funsigned-char", "-funsigned-bitfields",
     "-fpack-struct", "-fshort-enums"]

ldFlags         = common ++ ["-Wl,-Map=" ++ buildDir </> project <.> "map"]

hexFlashFlags   = ["-O", "ihex", "-R", ".eeprom", "-R", ".fuse", "-R", ".lock", "-R", ".signature"]
hexEepromFlags  = ["-O", "ihex", "-j", ".eeprom"]

avrdudeFlags    = ["-pm328p", "-cstk500v1", "-b57600", "-D"]

withSource f action out = action (f out) out
withDirExt dir ext = withSource (\name -> name `replaceDirectory` dir   `replaceExtension` ext)

main = shakeArgs shakeOptions $ do
    let defaultTargets = [project <.> "hex", project <.> "eep", project <.> "lss"]
    
    want defaultTargets
    
    phony "clean" $ do
        removeFilesAfter "." defaultTargets
        buildExists <- doesDirectoryExist buildDir
        when buildExists $ removeFilesAfter buildDir ["//*"]
    
    phony "flash" $ do
        avrdude avrdudeFlags usbPort (project <.> "hex")
    
    "*.hex" *> withDirExt buildDir "elf" (avr_objcopy hexFlashFlags)
    "*.eep" *> withDirExt buildDir "elf" (avr_objcopy hexEepromFlags)
    "*.lss" *> withDirExt buildDir "elf" avr_objdump
    
    buildDir </> "*.o" *> withDirExt srcDir "c" (avr_gcc cFlags)
    
    buildDir </> project <.> "elf" *> \out -> do
        srcs <- getDirectoryFiles srcDir ["*.c"]
        let objs = map (\src -> buildDir </> replaceExtension src "o") srcs
        avr_ld' "avr-gcc" ldFlags objs out
