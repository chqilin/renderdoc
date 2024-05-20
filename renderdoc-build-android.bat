echo Build for android-arm64
if exist build-android-arm64 rmdir /s /q build-android-arm64
mkdir build-android-arm64
cd build-android-arm64
cmake -DBUILD_ANDROID=On -DANDROID_ABI=arm64-v8a -GNinja ..
powershell -Command "(Get-Content build.ninja) -replace '&& c\+\+ 3rdparty/include-bin/main\.cpp', '&& \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" && cl.exe /Zi /EHsc /Fe:\".\bin\include-bin.exe\" 3rdparty/include-bin/main.cpp' | Set-Content build.ninja"
ninja
cd ..

echo Build for android-arm32
if exist build-android-arm64 rmdir /s /q build-android-arm32
mkdir build-android-arm32
cd build-android-arm32
cmake -DBUILD_ANDROID=On -DANDROID_ABI=armeabi-v7a -GNinja ..
powershell -Command "(Get-Content build.ninja) -replace '&& c\+\+ 3rdparty/include-bin/main\.cpp', '&& \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" && cl.exe /Zi /EHsc /Fe:\".\bin\include-bin.exe\" 3rdparty/include-bin/main.cpp' | Set-Content build.ninja"
ninja
cd ..

copy /Y build-android-arm64\bin\org.renderdoc.renderdoccmd.arm64.apk x64\Development\plugins\android\
copy /Y build-android-arm32\bin\org.renderdoc.renderdoccmd.arm32.apk x64\Development\plugins\android\

pause