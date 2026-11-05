package com.wechess.app

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.TextView
import kotlin.concurrent.thread

/**
 * Android wrapper: hosts the WebView, starts the C++ backend and provides the
 * system file chooser. It contains no chess logic and no JavaScript.
 */
class MainActivity : Activity() {

    private lateinit var webView: WebView
    private lateinit var status: TextView
    private var serverPort = 0
    private var fileCallback: ValueCallback<Array<Uri>>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = FrameLayout(this).apply { setBackgroundColor(BACKGROUND) }

        webView = WebView(this).apply {
            setBackgroundColor(BACKGROUND)
            visibility = View.INVISIBLE
        }
        root.addView(
            webView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )

        status = TextView(this).apply {
            setTextColor(TEXT)
            textSize = 15f
            setPadding(48, 72, 48, 48)
            text = "Starting WeChess\u2026"
        }
        root.addView(
            status,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        )

        setContentView(root)
        configureWebView()
        startBackend()
    }

    private fun startBackend() {
        thread(name = "wechess-server") {
            val port = NativeBridge.startServer(
                filesDir.absolutePath,
                applicationInfo.nativeLibraryDir,
                null
            )
            runOnUiThread {
                if (port > 0) {
                    serverPort = port
                    status.visibility = View.GONE
                    webView.visibility = View.VISIBLE
                    webView.loadUrl("http://127.0.0.1:$port/")
                } else {
                    status.text = buildString {
                        append("WeChess could not start its local server.\n\n")
                        append(NativeBridge.lastError())
                        append("\n\nIf Stockfish is missing, the board still works once the ")
                        append("server starts. See the README for packaging the engine.")
                    }
                }
            }
        }
    }

    private fun configureWebView() {
        val settings: WebSettings = webView.settings
        // The hard requirement: no JavaScript, ever.
        settings.javaScriptEnabled = false
        settings.javaScriptCanOpenWindowsAutomatically = false
        settings.domStorageEnabled = false
        settings.databaseEnabled = false
        settings.allowFileAccess = false
        settings.allowFileAccessFromFileURLs = false
        settings.allowUniversalAccessFromFileURLs = false
        settings.setGeolocationEnabled(false)
        settings.cacheMode = WebSettings.LOAD_NO_CACHE
        settings.useWideViewPort = true
        settings.loadWithOverviewMode = false
        settings.setSupportZoom(true)
        settings.builtInZoomControls = false
        settings.displayZoomControls = false
        settings.mediaPlaybackRequiresUserGesture = true
        settings.textZoom = 100
        // Needed so the system file chooser can hand the picked PGN to the
        // page's <input type="file">. The app never navigates to content:// URLs
        // itself (see shouldOverrideUrlLoading below).
        settings.allowContentAccess = true

        CookieManager.getInstance().setAcceptCookie(false)

        webView.webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(
                view: WebView,
                request: WebResourceRequest
            ): Boolean {
                val url = request.url
                val local = url.scheme == "http" &&
                    (url.host == "127.0.0.1" || url.host == "localhost") &&
                    url.port == serverPort
                // Anything that is not our own loopback UI is blocked outright.
                return !local
            }
        }

        webView.webChromeClient = object : WebChromeClient() {
            override fun onShowFileChooser(
                view: WebView?,
                callback: ValueCallback<Array<Uri>>?,
                params: FileChooserParams?
            ): Boolean {
                fileCallback?.onReceiveValue(null)
                fileCallback = callback
                return try {
                    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE)
                        type = "*/*"
                        putExtra(
                            Intent.EXTRA_MIME_TYPES,
                            arrayOf(
                                "application/x-chess-pgn",
                                "text/plain",
                                "application/octet-stream"
                            )
                        )
                    }
                    @Suppress("DEPRECATION")
                    startActivityForResult(intent, REQUEST_PGN)
                    true
                } catch (ex: Exception) {
                    fileCallback = null
                    false
                }
            }
        }
    }

    @Deprecated("Uses the platform activity result API without AndroidX")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_PGN) {
            val uri = if (resultCode == RESULT_OK) data?.data else null
            fileCallback?.onReceiveValue(if (uri != null) arrayOf(uri) else null)
            fileCallback = null
        }
    }

    @Deprecated("Simple back navigation without AndroidX")
    override fun onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack()
        } else {
            super.onBackPressed()
        }
    }

    override fun onDestroy() {
        webView.stopLoading()
        webView.loadUrl("about:blank")
        (webView.parent as? ViewGroup)?.removeView(webView)
        webView.destroy()
        NativeBridge.stopServer()
        super.onDestroy()
    }

    companion object {
        private const val REQUEST_PGN = 1001
        private val BACKGROUND = Color.parseColor("#14161A")
        private val TEXT = Color.parseColor("#F1EEE6")
    }
}
