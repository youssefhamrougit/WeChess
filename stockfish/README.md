# Stockfish

WeChess does not vendor a Stockfish binary. The app talks to a UCI engine process
and never re-implements a search, so the engine must be present on the device.

## Licensing first

Stockfish is **GPL-3.0-or-later**. An APK that ships Stockfish is a combined work,
so if you distribute it you must:

- provide the complete corresponding source (Stockfish **and** WeChess),
- keep the GPL notices, and
- grant recipients the GPL rights for the combined binary.

If that is not acceptable, ship WeChess without the binary: the interface still
works, and the review explains that no engine was found. You can also let users
install a Stockfish build themselves; Android 10+ blocks executing most binaries
from shared storage, so the supported path is the native-library route below.

## Desktop / server builds

Any Stockfish 16+ binary works:

```bash
# Debian/Ubuntu
sudo apt install stockfish
./build/wechess_server --stockfish /usr/games/stockfish

# or download an official release binary and point at it
WECHESS_STOCKFISH=$HOME/stockfish/stockfish-ubuntu-x86-64 \
  ./build/wechess_server --port 8080
```

The server also auto-detects `./stockfish/stockfish`, `/usr/games/stockfish`,
`/usr/bin/stockfish` and `/usr/local/bin/stockfish`. Settings → Engine path
overrides everything except an explicit `--stockfish`.

## Android builds

`scripts/fetch_stockfish_android.sh` clones Stockfish, cross-compiles it with the
NDK and installs it as an executable native library:

```bash
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.0.12077973
./scripts/fetch_stockfish_android.sh arm64-v8a
./scripts/fetch_stockfish_android.sh x86_64      # for the emulator
```

The file lands in `android/app/src/main/jniLibs/<abi>/libstockfish.so`. At runtime
`App::stockfishPath()` looks for `libstockfish.so` inside
`ApplicationInfo.nativeLibraryDir`, which is where Android extracts it
(`jniLibs.useLegacyPackaging = true` is required for that).

Notes:

- The network file (NNUE) must be embedded, otherwise Stockfish starts but cannot
  evaluate. Recent releases embed it by default; the script prints a check.
- Only `arm64-v8a` and `x86_64` are configured. Add `armeabi-v7a` in
  `android/app/build.gradle.kts` if you need 32-bit devices, and build the engine
  for the same ABI.
- The engine runs as a child process with a pipe pair; Android's seccomp policy
  allows `exec` from the app's native library directory.
