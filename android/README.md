# SyncFlow Android - Setup & Build Guide

**Building SyncFlow for Android: Complete Setup and Development Guide**

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Prerequisites Installation](#prerequisites-installation)
3. [Project Structure](#project-structure)
4. [Building from Source](#building-from-source)
5. [Running on Device](#running-on-device)
6. [Development Workflow](#development-workflow)
7. [Troubleshooting](#troubleshooting)
8. [Advanced Build Options](#advanced-build-options)

---

## System Requirements

### Minimum Requirements

- **Android Version**: API 26 (Android 8.0) or higher
- **Device RAM**: 512 MB minimum (1 GB+ recommended for development)
- **Device Storage**: 100 MB free
- **Network**: Broadband connection for sync operations

### Build Machine Requirements

- **OS**: Linux, macOS, or Windows 10+
- **RAM**: 8 GB (4 GB minimum)
- **Disk**: 15 GB free (Android SDK + NDK + project + builds)
- **Java**: JDK 11 or higher

### Software Requirements

- **Android Studio**: 2021.3.1 or newer (or use Android SDK tools)
- **Android SDK**: API 26+ (automatically installed by Android Studio)
- **Android NDK**: r23 or newer (for C++ compilation)
- **CMake**: 3.20+ (for NDK builds)
- **Gradle**: 7.0+ (usually bundled with Android Studio)

---

## Prerequisites Installation

### Quickstart (One-Command Setup)

If you're on Linux/macOS, here's the fastest way to get started:

```bash
# 1. Install Java
sudo apt-get update && sudo apt-get install -y openjdk-11-jdk  # Ubuntu/Debian
# or: brew install java11  (macOS)

# 2. Download Android Studio
cd ~
wget https://redirector.gstatic.com/android/studio/install/2024.2.1.10/android-studio-2024.2.1.10-linux.tar.gz
tar -xzf android-studio-*-linux.tar.gz

# 3. Launch Android Studio and let it auto-install everything
./android-studio/bin/studio.sh
```

Android Studio will prompt you to install SDK, NDK, and CMake automatically on first run. Accept all defaults.

### Manual Setup (Detailed)

#### Step 1: Verify Java Installation

```bash
java -version
# Should output: openjdk version "11.x.x"
```

#### Step 2: Download & Launch Android Studio

**On Linux:**

```bash
cd ~
wget https://redirector.gstatic.com/android/studio/install/2024.2.1.10/android-studio-2024.2.1.10-linux.tar.gz
tar -xzf android-studio-*-linux.tar.gz
./android-studio/bin/studio.sh
```

**On macOS:**

```bash
brew install android-studio
android-studio
```

**On Windows:**
Download from [developer.android.com/studio](https://developer.android.com/studio) and run installer.

#### Step 3: First Launch - SDK Setup Wizard

When Android Studio launches for the first time:

1. **Welcome Screen** → Click **"Next"**
2. **Setup Type** → Select **"Standard"** → Click **"Next"**
3. **UI Theme** → Choose **"Light"** or **"Dark"** → Click **"Next"**
4. **SDK Components Setup** → Let it download (5-15 minutes):
   - Android SDK
   - Android SDK Build-Tools
   - Android Emulator
   - Android Virtual Device (AVD)
5. Click **"Finish"** when complete

**Don't skip this step** - Android Studio needs these components.

#### Step 4: Install Required Packages

Once Android Studio opens:

1. Go to **Tools → SDK Manager**
2. Click **"SDK Platforms"** tab
3. Check these boxes:
   - ✅ Android 14 (API 34)
   - ✅ Android 8.0 (API 26) ← Minimum for SyncFlow
4. Click **"SDK Tools"** tab
5. Check these boxes:
   - ✅ Android SDK Build-Tools
   - ✅ NDK (Side by side) ← Important for C++
   - ✅ CMake ← Important for C++
6. Click **"Apply"** and wait for installation

#### Step 5: Verify Installation

```bash
# Check SDK location
ls ~/Android/Sdk/platforms  # Should see android-26, android-34, etc
ls ~/Android/Sdk/ndk        # Should see version folder (e.g., 26.1.10909125)

# Check CMake
ls ~/Android/Sdk/cmake      # Should see cmake folder
```

If any folder is empty, re-run SDK Manager and install missing components.

---

## Android Studio Setup & Configuration

### Step 1: Download Android Studio

#### On Windows:

1. Visit [developer.android.com/studio](https://developer.android.com/studio)
2. Click **"Download Android Studio"**
3. Accept license agreement
4. Download installer (exe file, ~900 MB)
5. Run installer and follow wizard
6. Choose installation location (default: `C:\Program Files\Android\Android Studio`)
7. Accept default components
8. Click **Finish** when complete

#### On macOS:

1. Visit [developer.android.com/studio](https://developer.android.com/studio)
2. Click **"Download Android Studio"**
3. Download DMG file (Intel or Apple Silicon version)
4. Open DMG and drag Android Studio to Applications
5. Launch Android Studio from Applications folder

**Or use Homebrew:**

```bash
brew install android-studio
```

#### On Linux:

1. Visit [developer.android.com/studio](https://developer.android.com/studio)
2. Download Linux version (TAR.GZ file, ~900 MB)
3. Extract and run:

```bash
# Extract
tar -xzf android-studio-2024.1.1.11-linux.tar.gz

# Run
./android-studio/bin/studio.sh

# Optional: Create shortcut
sudo ln -s ~/android-studio/bin/studio.sh /usr/local/bin/android-studio
```

### Step 2: First Launch & Welcome Wizard

**On first launch, Android Studio shows Welcome wizard:**

1. **Welcome Screen**
   - Read welcome message
   - Click **"Next"** to continue

2. **Setup Type Selection**
   - Choose **"Standard"** (recommended for most users)
   - Includes Android SDK, Platform Tools, Emulator

---

## Opening SyncFlow in Android Studio

### Step 1: Open the Project

1. **Launch Android Studio** (should still be open from setup)
2. **File → Open**
3. Navigate to: `/path/to/syncflow/android/`
4. Click **"Open"**

### Step 2: Wait for Gradle Sync

Android Studio will automatically sync Gradle. Watch the status bar at the bottom:

```
Gradle sync in progress...
Gradle sync finished successfully ✓
```

If you see errors:

- Right-click the error → "Run 'gradle sync'"
- Or: File → Sync Now

**First sync takes 2-5 minutes** - be patient.

### Step 3: Verify Everything is Working

Once sync completes:

1. **Build → Make Project** (Ctrl+F9)
   - Should complete without errors
   - You'll see: **"Build completed successfully"**

2. **Check logcat** (View → Tool Windows → Logcat)
   - Should see Android system logs, no errors

3. **Device Manager** (click icon in toolbar)
   - Should show at least one emulator or connected device

### Step 4: Build and Run

```bash
# Method 1: Using Android Studio GUI
# - Select emulator/device from dropdown (near Run button)
# - Click green "Run" button or press Shift+F10
# - App should install and launch

# Method 2: Using command line
cd /path/to/syncflow/android
./gradlew assembleDebug        # Build APK
./gradlew installDebug         # Install on device
adb shell am start -n com.syncflow/.MainActivity  # Launch
```

---

## Project Structure

```
syncflow/android/
├── README.md                              # This file
├── build.gradle.kts                       # Root build configuration
├── settings.gradle.kts                    # Project settings
├── gradle.properties                      # Gradle properties
├── local.properties                       # SDK/NDK paths (auto-generated, don't edit)
│
├── app/                                   # Main Android app module
│   ├── build.gradle.kts                   # App-level build config
│   ├── proguard-rules.pro                 # Obfuscation rules
│   └── src/
│       ├── main/
│       │   ├── AndroidManifest.xml        # App manifest
│       │   ├── java/com/syncflow/
│       │   │   ├── MainActivity.kt        # Main UI
│       │   │   ├── SyncService.kt         # Background service
│       │   │   └── ...                    # Other components
│       │   ├── cpp/
│       │   │   ├── CMakeLists.txt         # C++ build config
│       │   │   ├── jni_bridge.hpp         # JNI interface
│       │   │   └── jni_bridge.cpp         # JNI implementation
│       │   └── res/
│       │       ├── layout/                # XML layouts
│       │       ├── values/                # Strings, colors, styles
│       │       └── drawable/              # Images
│       ├── test/                          # Unit tests
│       └── androidTest/                   # Device tests
│
├── gradle/wrapper/                        # Gradle wrapper files
└── .gradle/                               # Gradle cache (auto-generated)
```

---

## Quick Troubleshooting

### Gradle Sync Fails

```bash
# Clean and resync
./gradlew clean
File → Sync Now

# Or
./gradlew --refresh-dependencies
```

### "NDK not found" Error

1. Tools → SDK Manager
2. SDK Tools tab
3. Check "NDK (Side by side)"
4. Click Apply
5. Wait for installation
6. File → Sync Now

### "CMake not found" Error

1. Tools → SDK Manager
2. SDK Tools tab
3. Check "CMake"
4. Click Apply
5. File → Sync Now

### App Won't Install on Device

```bash
# Check device is connected
adb devices

# If not listed:
# 1. Enable USB Debugging on phone (Settings → Developer Options)
# 2. Disconnect and reconnect USB
# 3. Grant USB debugging permission on phone when prompted

# Try again
./gradlew installDebug
```

### Build is Very Slow

Add to `gradle.properties`:

```properties
org.gradle.parallel=true
org.gradle.workers.max=8
org.gradle.daemon=true
org.gradle.jvmargs=-Xmx4096m
```

Then rebuild.

---

## Running the App

### On Emulator

1. Tools → Device Manager (or click Device Manager icon)
2. Click **"▶"** (play) button on emulator
3. Wait 30-60 seconds for boot
4. Run → Run 'app' (Shift+F10)
5. Select the emulator
6. Click OK

### On Physical Device

1. Connect via USB
2. Settings → Developer Options → USB Debugging (ON)
3. Accept USB debugging permission on phone
4. Run → Run 'app' (Shift+F10)
5. Select your phone
6. Click OK

### View Logs

```bash
# In Android Studio
View → Tool Windows → Logcat (or Alt+6)

# Filter for SyncFlow
Type in search box: "syncflow"

# From command line
adb logcat | grep syncflow
```

---

## Building APK Files

### Debug APK (for testing)

```bash
./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

### Release APK (for distribution)

```bash
./gradlew assembleRelease
# Output: app/build/outputs/apk/release/app-release-unsigned.apk
```

### Build and Install in One Command

```bash
./gradlew installDebug    # Build + install debug APK
adb logcat                # View logs while running
```

---

## Common Commands

```bash
# Navigate to Android project
cd /path/to/syncflow/android

# List available Gradle tasks
./gradlew tasks

# Clean build directory
./gradlew clean

# Full rebuild
./gradlew clean build

# Build without tests (faster)
./gradlew build -x test

# Run unit tests
./gradlew test

# Run device tests
./gradlew connectedAndroidTest

# Build and install debug APK
./gradlew installDebug

# Uninstall app from device
adb uninstall com.syncflow

# View device logs
adb logcat

# Stop Gradle daemon (if stuck)
./gradlew --stop
```

---

## Testing on Device

### Unit Tests (runs on JVM)

```bash
./gradlew test
```

### Instrumentation Tests (runs on device/emulator)

```bash
./gradlew connectedAndroidTest
```

### View Test Results

```
app/build/reports/tests/debug/index.html    # Unit test report
app/build/reports/androidTests/connected/   # Device test results
```

---

## Getting Help

### If Something Breaks

1. **Check Android Studio Logcat:** View → Tool Windows → Logcat
2. **Check Gradle output:** Run the command again and read error message carefully
3. **Check the Troubleshooting section below** for your specific error
4. **Search Google:** Copy error message into Google search
5. **Check StackOverflow:** Tag: `android` + `gradle` + `android-studio`

### Common Issues

| Error                 | Solution                                          |
| --------------------- | ------------------------------------------------- |
| Gradle sync fails     | `./gradlew clean` then File → Sync Now            |
| NDK not found         | Tools → SDK Manager → SDK Tools → Install NDK     |
| CMake not found       | Tools → SDK Manager → SDK Tools → Install CMake   |
| App won't install     | Connect device, enable USB Debugging, try again   |
| Debugger won't attach | `adb kill-server` then `adb start-server`         |
| Build is very slow    | Add properties to `gradle.properties` (see above) |

---

## Project Structure

```
syncflow/android/
├── README.md                              # This file
├── build.gradle.kts                       # Root build configuration
├── settings.gradle.kts                    # Project settings
├── gradle.properties                      # Gradle properties
├── local.properties                       # Local SDK/NDK paths (auto-generated)
│
├── app/                                   # Main Android app module
│   ├── build.gradle.kts                   # App-level build config
│   ├── proguard-rules.pro                 # ProGuard/R8 obfuscation rules
│   ├── src/
│   │   ├── main/
│   │   │   ├── AndroidManifest.xml        # App manifest
│   │   │   ├── res/                       # Android resources (layouts, strings, etc)
│   │   │   │   ├── values/                # String resources, colors, styles
│   │   │   │   ├── drawable/              # Images and drawables
│   │   │   │   └── layout/                # XML layouts
│   │   │   ├── java/com/syncflow/         # Kotlin/Java source
│   │   │   │   ├── MainActivity.kt        # Main UI activity
│   │   │   │   ├── SyncService.kt         # Background sync service
│   │   │   │   └── ...                    # Other components
│   │   │   └── cpp/                       # C++ JNI code
│   │   │       ├── jni_bridge.hpp         # JNI interface
│   │   │       ├── jni_bridge.cpp         # JNI implementation
│   │   │       └── CMakeLists.txt         # C++ build config
│   │   ├── test/                          # Unit tests (runs on JVM)
│   │   └── androidTest/                   # Instrumentation tests (runs on device)
│   └── build/                             # Build outputs (auto-generated)
│
├── gradle/                                # Gradle wrapper
│   └── wrapper/
│       ├── gradle-wrapper.jar
│       └── gradle-wrapper.properties
│
└── .gradle/                               # Gradle cache (auto-generated)
```

---

## Building from Source

### Quick Start

```bash
cd /path/to/syncflow/android

# Build debug APK
./gradlew assembleDebug

# Build and install to connected device
./gradlew installDebug

# View logs
adb logcat | grep syncflow
```

### Build Options

```bash
# Clean build
./gradlew clean build

# Debug build (faster)
./gradlew assembleDebug

# Release build (optimized)
./gradlew assembleRelease

# Build without running tests (faster)
./gradlew build -x test

# Build only C++ native libraries
./gradlew build -DenableNativeDebug=true
```

---

## Verification Checklist

After setup, verify everything works:

- [ ] Android Studio launches and opens SyncFlow project
- [ ] Gradle sync completes with no errors
- [ ] "Build → Make Project" succeeds
- [ ] Device Manager shows at least one emulator or device
- [ ] "Run 'app'" installs app on device/emulator
- [ ] App launches without crashing
- [ ] Logcat shows no errors
- [ ] adb devices lists connected device

---

## Useful Resources

- **Android Documentation:** [developer.android.com](https://developer.android.com)
- **Android Studio Guide:** [developer.android.com/studio](https://developer.android.com/studio)
- **NDK Documentation:** [developer.android.com/ndk](https://developer.android.com/ndk)
- **Gradle Documentation:** [gradle.org](https://gradle.org)---

## Development Workflow

### Edit → Build → Run Cycle

```bash
# 1. Edit code (.kt, .cpp, or resources)
# 2. Build in Android Studio (Ctrl+F9) or run:
./gradlew build

# 3. Run on device/emulator (Shift+F10 in Studio or):
./gradlew installDebug

# 4. View logs:
adb logcat | grep syncflow
```

### Debugging

**In Android Studio:**

- Set breakpoint by clicking line number
- Run → Debug 'app' (Shift+F9)
- Use Debugger window to inspect variables

**From command line:**

```bash
# For C++ code
./gradlew assembleDebug -DenableNativeDebug=true

# For Java/Kotlin code
./gradlew installDebug
adb shell gdb
```

### Testing

```bash
# Unit tests (JVM)
./gradlew test

# Device tests
./gradlew connectedAndroidTest
```

---

## System Troubleshooting

| Problem               | Solution                                                                            |
| --------------------- | ----------------------------------------------------------------------------------- |
| Gradle sync fails     | `./gradlew clean` then File → Sync Now in Studio                                    |
| NDK not found         | Tools → SDK Manager → SDK Tools → Install NDK                                       |
| CMake not found       | Tools → SDK Manager → SDK Tools → Install CMake                                     |
| Build is slow         | Add to gradle.properties: `org.gradle.parallel=true` `org.gradle.jvmargs=-Xmx4096m` |
| App won't install     | Connect device, enable USB Debugging, try again                                     |
| Device not recognized | `adb kill-server && adb start-server`                                               |
| App crashes           | Check logcat: `adb logcat \| grep syncflow`                                         |
| Can't debug           | `adb kill-server && adb start-server` then retry Shift+F9                           |

---

## Release Checklist

Before release:

- [ ] Build with `./gradlew assembleRelease`
- [ ] App runs on min API 26 and latest API 34+
- [ ] No logcat errors
- [ ] Sync connects to peers
- [ ] File sync works both ways
- [ ] No memory leaks

---

## References

- **Android Docs:** [developer.android.com](https://developer.android.com)
- **NDK Guide:** [developer.android.com/ndk](https://developer.android.com/ndk)
- **Gradle Docs:** [gradle.org](https://gradle.org)

---

**Last Updated:** 2026-04-23  
**Project Version:** 0.1.0  
**Min Android:** API 26 (Android 8.0)  
**Target Android:** API 34 (Android 14)
