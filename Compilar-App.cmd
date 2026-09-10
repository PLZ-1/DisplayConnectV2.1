@echo off
setlocal
if not defined JAVA_HOME set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if exist "%JAVA_HOME%\bin\java.exe" set "Path=%JAVA_HOME%\bin;%Path%"
pushd "%~dp0"
if errorlevel 1 exit /b 1
call gradlew.bat --no-daemon --console=plain :app:assembleDebug :app:testDebugUnitTest
if errorlevel 1 goto failed
echo APK: "%CD%\app\build\outputs\apk\debug\app-debug.apk"
popd
exit /b 0
:failed
echo ERRO: confira a mensagem acima, o JDK 21 e a configuracao do SDK Android.
popd
exit /b 1
