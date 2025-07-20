# Dun-Fang-Hun-Jumpscare
[![License](https://img.shields.io/badge/license-The_Unlicense-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/build-Passing-default.svg)](https://travis-ci.org/username/project)

蹲方魂不语 只是一味faker我的bin

### Features

- The application starts and minimizes to the system tray.
- Right-clicking the tray icon provides an option to exit.
- Pressing the F1 key triggers the application's core function, which will be talked about then:
- When activated, it plays a sound from the same directory and displays images from the same directory in sequential order. They are 蹲方魂 memes by default.
- Although it isn't my goal, you can change the images and the audio to your own ones. As a notice, the images' filenames have to be like ```01.png, 02.png...``` and the sound's filename has to be ```00.wav```.

### Compiling
```
g++ Jumpscare.cpp -o Jumpscare.exe -lgdiplus -lwinmm -lcomctl32 -mwindows -O2 -std=c++11
```

### Uninstall
Simply delete the executable. No system files or registry changes are made.
