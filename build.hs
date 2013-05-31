#!/usr/bin/env runhaskell
module Main where

import Control.Monad
import Development.Shake
import Development.Shake.Command
import Development.Shake.FilePath

srcDir          = "src"
buildDir        = "build" -- NB: "clean" will blast this dir.

project         = "NixieClock"
mcu             = "atmega328p"
cpp             = "avr-cpp"
cc              = "avr-gcc"
objcopy         = "avr-objcopy"
objdump         = "avr-objdump"

common          = ["-mmcu=" ++ mcu]

cppFlags        = common ++ ["-DF_CPU=16000000UL", "-Iinclude"]
cFlags          = cppFlags ++ 
    ["-Wall", "-gdwarf-2", "-std=gnu99", "-save-temps=obj",
     "-Os", "-funsigned-char", "-funsigned-bitfields",
     "-fpack-struct", "-fshort-enums"]

ldFlags         = common ++ ["-Wl,-Map=" ++ buildDir </> "NixieClock.map"]

hexCommon       = ["-O", "ihex"]
hexFlashFlags   = hexCommon ++ ["-R", ".eeprom", "-R", ".fuse", "-R", ".lock", "-R", ".signature"]
hexEepromFlags  = hexCommon ++ ["-j", ".eeprom"]

gccDeps src    = do
    Stdout cppOut <- command [] cpp (cppFlags ++ ["-M", "-MG", "-E", src])
    return (filter (/= "\\") (drop 2 (words cppOut)))

main = shakeArgs shakeOptions $ do
    let defaultTargets = ["NixieClock.hex", "NixieClock.eep", "NixieClock.lss"]
    want defaultTargets
    
    phony "clean" $ do
        removeFilesAfter "." defaultTargets
        removeFilesAfter buildDir ["//*"]
    
    buildDir </> "NixieClock.elf" *> \out -> do
        srcs <- getDirectoryFiles srcDir ["*.c"]
        let objs = map ((buildDir </>) . flip replaceExtension "o") srcs
        need objs
        system' cc (ldFlags ++ ["-o", out] ++ objs)
    
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
    
    [buildDir </> "*.o", buildDir </> "*.i", buildDir </> "*.s"] *>> \[out, _, _] -> do
        let src = replaceExtension (replaceDirectory out srcDir) "c"
        need [src]
        need =<< gccDeps src
        system' cc (cFlags ++ ["-c", "-o", out, src])
        