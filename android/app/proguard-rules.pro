# ProGuard rules for SyncFlow

# Keep SyncFlow classes
-keep class com.syncflow.** { *; }
-keep interface com.syncflow.** { *; }

# Keep native methods
-keepclasseswithmembernames class ** {
    native <methods>;
}

# Keep AndroidX
-keep class androidx.** { *; }
-keep interface androidx.** { *; }

# Keep Kotlin metadata
-keepclassmembers class ** {
    *** Companion;
}
-keepclasseswithmembernames class ** {
    native <methods>;
}

# Logging
-assumenosideeffects class android.util.Log {
    public static *** d(...);
    public static *** v(...);
    public static *** i(...);
}
