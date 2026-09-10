@echo off
setlocal
echo [1/2] Compilando e testando o Android...
call "%~dp0Compilar-App.cmd"
if errorlevel 1 exit /b 1
echo [2/2] Compilando o ESP32...
call "%~dp0Compilar-ESP32.cmd"
if errorlevel 1 exit /b 1
echo CONCLUIDO. Instale o APK e grave o ESP32 para testar esta versao.
exit /b 0
