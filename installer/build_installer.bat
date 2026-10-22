@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\portBurnout3\installer"

echo Compilando recursos...
rc.exe /v installer.rc

echo Compilando instalador nativo...
cl.exe /nologo /O2 /W3 /std:c++17 /EHsc installer.cpp installer.res /Fe:Instalador_Burnout3_PC.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib uuid.lib

if exist Instalador_Burnout3_PC.exe (
    echo Exito! Instalador compilado correctamente.
) else (
    echo Error durante la compilacion.
)
