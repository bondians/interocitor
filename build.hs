#!/usr/bin/env runhaskell
{-# OPTIONS_GHC -fwarn-unused-imports -fwarn-unused-binds #-}
module Main (main) where

import Development.Shake
import Development.Shake.Command
import Development.Shake.FilePath

srcDir          = "src"
buildDir        = "build" -- NB: "clean" will blast this dir.

usbPort         = "/dev/tty.usbserial-A70075bH"

project         = "NixieClock"
mcu             = "atmega328p"
cpp             = "avr-cpp"
cc              = "avr-gcc"
objcopy         = "avr-objcopy"
objdump         = "avr-objdump"
avrdude         = "avrdude"

common          = ["-mmcu=" ++ mcu]

cppFlags        = common ++ ["-DF_CPU=16000000UL", "-Iinclude"]
cFlags          = cppFlags ++ 
    ["-Wall", "-gdwarf-2", "-std=gnu99",
     "-Os", "-funsigned-char", "-funsigned-bitfields",
     "-fpack-struct", "-fshort-enums"]

ldFlags         = common ++ ["-Wl,-Map=" ++ buildDir </> project <.> "map"]

hexCommon       = ["-O", "ihex"]
hexFlashFlags   = hexCommon ++ ["-R", ".eeprom", "-R", ".fuse", "-R", ".lock", "-R", ".signature"]
hexEepromFlags  = hexCommon ++ ["-j", ".eeprom"]

avrdudeFlags    = ["-pm328p", "-cstk500v1", "-b57600", "-D"]

gccDeps src    = do
    Stdout cppOut <- command [] cpp (cppFlags ++ ["-M", "-MG", "-E", src])
    return (filter (/= "\\") (drop 2 (words cppOut)))

main = shakeArgs shakeOptions $ do
    let defaultTargets = [project <.> "hex", project <.> "eep", project <.> "lss"]
    want defaultTargets
    
    phony "clean" $ do
        removeFilesAfter "." defaultTargets
        removeFilesAfter buildDir ["//*"]
    
    phony "flash" $ do
        alwaysRerun
        let hex = project <.> "hex"
        need [hex, usbPort]
        system' avrdude (avrdudeFlags ++ ["-P", usbPort, "-Uflash:w:" ++ hex])
    
    "*.hex" *> \out -> do
        let src = buildDir </> replaceExtension out "elf"
        need [src]
        system' objcopy (hexFlashFlags ++ [src, out])
    
    "*.eep" *> \out -> do
        let src = buildDir </> replaceExtension out "elf"
        need [src]
        system' objcopy (hexEepromFlags ++ [src, out])
    
    "*.lss" *> \out -> do
        let src = buildDir </> replaceExtension out "elf"
        need [src]
        Stdout lss <- command [] objdump ["-h", "-S", src]
        writeFileChanged out lss
    
    buildDir </> project <.> "elf" *> \out -> do
        srcs <- getDirectoryFiles srcDir ["*.c"]
        let objs = map (\src -> buildDir </> replaceExtension src "o") srcs
        need objs
        system' cc (ldFlags ++ ["-o", out] ++ objs)
    
    buildDir </> "*.o" *> \out -> do
        let src = replaceExtension (replaceDirectory out srcDir) "c"
        need [src]
        need =<< gccDeps src
        system' cc (cFlags ++ ["-c", "-o", out, src])
        