@echo off
rem ============================================================
rem  PDF Page Extractor - 构建脚本（MinGW-w64 g++）
rem  用法：双击运行，或在命令行执行 build.bat
rem  产物：dist\PdfPageExtractor.exe
rem ============================================================
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 g++，请先安装 MinGW-w64 并将其 bin 目录加入 PATH。
    exit /b 1
)

if not exist build mkdir build
if not exist dist mkdir dist

echo [1/3] 编译资源文件...
windres --use-temp-file src\app.rc -O coff -o build\app.res.o
if errorlevel 1 goto :fail

echo [2/3] 编译源代码...
gcc -O2 -c -DNDEBUG src\third_party\miniz\miniz.c -o build\miniz.o
if errorlevel 1 goto :fail
g++ -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -municode ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -Wall -Wextra -Wno-unused-parameter ^
    -Isrc -Isrc\third_party ^
    src\main.cpp src\pdfextract.cpp src\xlsx.cpp build\miniz.o ^
    build\app.res.o ^
    -o dist\PdfPageExtractor.exe ^
    -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -lkernel32
if errorlevel 1 goto :fail

echo [3/3] 完成！
echo 产物: dist\PdfPageExtractor.exe
exit /b 0

:fail
echo [失败] 编译出错，请查看上方输出。
exit /b 1
