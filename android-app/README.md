# Syncflow Android App

This folder contains the Android Studio project for the Syncflow mobile app.

## Requirements

- Android Studio (latest stable recommended)
- Android SDK 35 or newer
- Java 17
- An Android device or emulator

## Open in Android Studio

1. Start Android Studio.
2. Select **Open**.
3. Open this folder:
   - `android-app`
4. Wait for Gradle sync to finish.

## Build the app

In Android Studio:

1. Click **Build** → **Make Project**.
2. Or use **Build** → **Build Bundle(s) / APK(s)** → **Build APK(s)**.

## Run the app

1. Connect an Android device or start an emulator.
2. Click **Run** in Android Studio.
3. Select the target device.

## What the app does

- Uses a native Android UI (Quick Share style, not a web view)
- Lets you set the Syncflow endpoint
- Provides native controls for:
  - discovery start/stop/list/status
  - transfer send/receiver start/stop/status
  - sync start/stop/status
  - background on/off
- Shows operation logs directly in-app

## Background mode

The app includes a foreground background service and a boot receiver.

- Tap **Background On** in the app to keep Syncflow active
- The app can restart its background service after reboot or app update

## Notes

- If you want the app to control a local Syncflow instance, run `syncflow_ui` on the same device or another device on the same network.
- For Android 13+ you may need to allow notifications for the foreground service.
- If the endpoint is remote, use the LAN IP address instead of `127.0.0.1`.

## Project structure

- `app/src/main/java/com/syncflow/android/`
  - `MainActivity.kt`
  - `SyncflowBackgroundService.kt`
  - `BootReceiver.kt`
- `app/src/main/res/`
  - layouts, strings, colors, themes

## Quick checks

If the app does not build:

- Make sure Gradle sync completed successfully
- Make sure Java 17 is selected in Android Studio
- Make sure the Android SDK is installed
- Make sure the project is opened from the `android-app` folder
