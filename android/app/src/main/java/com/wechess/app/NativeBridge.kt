package com.wechess.app

/**
 * Thin JNI surface: start/stop the local C++ backend. No application logic lives
 * here, and the WebView never gets JavaScript.
 */
object NativeBridge {
    init {
        System.loadLibrary("wechess")
    }

    external fun startServer(dataDir: String, nativeLibDir: String, stockfishPath: String?): Int
    external fun stopServer()
    external fun lastError(): String
}
