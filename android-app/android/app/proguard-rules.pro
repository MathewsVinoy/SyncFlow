zc# ProGuard rules for SyncFlow
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# Keep all classes in the app package
-keep class com.syncflow.** { *; }

# Keep Kotlin metadata
-keepattributes *Annotation*
-keep class kotlin.** { *; }

# Keep Room database classes
-keep class * extends androidx.room.RoomDatabase { *; }
-keep @androidx.room.Entity class * { *; }
-keep @androidx.room.Dao interface * { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep OpenSSL classes if linked
-keep class com.google.crypto.tink.** { *; }
