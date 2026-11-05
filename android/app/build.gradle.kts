plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.wechess.app"
    compileSdk = 35
    // Pin the NDK so the native build is reproducible.
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.wechess.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DWECHESS_BUILD_TESTS=OFF",
                    "-DWECHESS_BUILD_PROXY=OFF",
                    // Set to ON only after cross-compiling curl for Android.
                    "-DWECHESS_WITH_CURL=OFF"
                )
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        // Stockfish is shipped as an executable file in jniLibs; legacy
        // packaging makes Android extract native libraries to disk so they can
        // actually be exec'd from applicationInfo.nativeLibraryDir.
        jniLibs {
            useLegacyPackaging = true
            keepDebugSymbols += "**/libwechess.so"
        }
    }
}

dependencies {
    // Intentionally empty: the wrapper only uses the Android platform APIs.
}
