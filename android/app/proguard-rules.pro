# JNI entry points are called from Kotlin and keep their native names.
-keepclasseswithmembernames class com.wechess.app.NativeBridge {
    native <methods>;
}
