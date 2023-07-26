@echo off

set cfg=Release

:loop
IF NOT "%1"=="" (
    IF "%1"=="-debug" (
        SET cfg=Debug
        SHIFT
    )
    IF "%1"=="-release" (
        SET cfg=Release
        SHIFT
    )
    SHIFT
    GOTO :loop
)

mkdir _bin
cmake --build _build --config %cfg%