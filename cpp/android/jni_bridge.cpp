// WeChess - Android bridge.
//
// The Kotlin side only starts/stops the backend and hosts the WebView; every
// piece of application logic stays in C++.
#include <memory>
#include <mutex>
#include <string>

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>

#include "../server/app.hpp"
#include "../server/http.hpp"

namespace {

constexpr const char* kTag = "WeChess";
std::mutex gMutex;
std::unique_ptr<wechess::App> gApp;
std::unique_ptr<wechess::HttpServer> gServer;
std::string gLastError;

std::string toStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) return std::string();
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return std::string();
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

void shutdownLocked() {
    if (gServer) {
        gServer->stop();
        gServer.reset();
    }
    gApp.reset();
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_wechess_app_NativeBridge_startServer(JNIEnv* env, jobject /*thiz*/, jstring dataDir,
                                              jstring nativeLibDir, jstring stockfishPath) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gServer) return gServer->port();

    const std::string data = toStdString(env, dataDir);
    const std::string libs = toStdString(env, nativeLibDir);
    const std::string engine = toStdString(env, stockfishPath);

    gApp.reset(new wechess::App(data, libs, engine));
    std::string error;
    if (!gApp->init(error)) {
        gLastError = error;
        __android_log_print(ANDROID_LOG_ERROR, kTag, "init failed: %s", error.c_str());
        gApp.reset();
        return -1;
    }

    gServer.reset(new wechess::HttpServer());
    gServer->handler = [](const wechess::HttpRequest& request) { return gApp->handle(request); };
    if (!gServer->listen("127.0.0.1", 0, error)) {
        gLastError = error;
        __android_log_print(ANDROID_LOG_ERROR, kTag, "listen failed: %s", error.c_str());
        shutdownLocked();
        return -1;
    }
    gServer->start();
    gLastError.clear();
    __android_log_print(ANDROID_LOG_INFO, kTag, "backend listening on port %d", gServer->port());
    return gServer->port();
}

extern "C" JNIEXPORT void JNICALL
Java_com_wechess_app_NativeBridge_stopServer(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(gMutex);
    shutdownLocked();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wechess_app_NativeBridge_lastError(JNIEnv* env, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(gMutex);
    return env->NewStringUTF(gLastError.c_str());
}

#else

// Desktop builds do not include <jni.h>; the file is still compiled into the
// source list checks but exports nothing.
extern "C" void wechess_jni_bridge_desktop_stub(void) {}

#endif  // __ANDROID__
