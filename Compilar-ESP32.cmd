@echo off
setlocal
if not defined ARDUINO_CLI set "ARDUINO_CLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if not exist "%ARDUINO_CLI%" goto missingCli
if not exist "%~dp0DisplaySender\config.h" copy /y "%~dp0DisplaySender\config.h.example" "%~dp0DisplaySender\config.h" >nul
if not exist "%~dp0DisplaySender\config.h" goto failed
"%ARDUINO_CLI%" compile --fqbn esp32:esp32:lolin32-lite --build-path "%TEMP%\DisplayConnect-ST7796S-build" --output-dir "%~dp0hardware\firmware" "%~dp0DisplaySender"
if errorlevel 1 goto failed
echo Firmware: "%~dp0hardware\firmware"
echo Para gravar, abra o sketch no Arduino IDE e use Carregar.
exit /b 0
:missingCli
echo ERRO: defina ARDUINO_CLI com o caminho do arduino-cli.exe instalado.
exit /b 1
:failed
echo ERRO: confira a mensagem acima, a placa e as bibliotecas descritas no README.
exit /b 1
