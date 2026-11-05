# Deployment: GitHub, GitHub Actions, Vercel, APK

Everything needed to publish WeChess is in the repository. This page covers the
three parts: the ZIP you can hand to someone, the GitHub Actions build that
produces the APK, and the Vercel landing page.

## 0. What cannot be done from the development sandbox

The environment where WeChess was written has no repository remote and no
credentials for GitHub or Vercel, so the uploads themselves have to run on your
machine. Nothing else is missing: the workflows, the site and the packaging
script are complete and ready to use.

Check what you have:

```bash
git remote -v          # empty means "no remote configured yet"
gh auth status         # optional: GitHub CLI
npx vercel whoami      # optional: Vercel CLI
```

## 1. The ZIP

```bash
./scripts/package_zip.sh
# -> dist/wechess-1.0.0-source.zip  (+ .sha256)
```

It contains the full project — C++ sources, frontend, Android project,
workflows, tests, docs, licenses — and excludes build output, `.tools/`,
keystores and APKs. Recipients extract it and follow the README:

```bash
unzip wechess-1.0.0-source.zip -d wechess
cd wechess
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/wechess_server --port 8080 --stockfish /usr/games/stockfish
```

## 2. Push to GitHub

```bash
git init -b main                     # if this directory is not a repo yet
git add .
git commit -m "WeChess: chess game review with Stockfish, zero JavaScript UI"
gh repo create your-org/wechess --public --source=. --push
# or, with an existing empty repo:
# git remote add origin git@github.com:your-org/wechess.git
# git push -u origin main
```

Replace `OWNER/REPO` in `site/index.html` (three links) before deploying:

```bash
sed -i 's|OWNER/REPO|your-org/wechess|g' site/index.html
```

## 3. GitHub Actions → APK

Workflow: `.github/workflows/android.yml`. It runs on every push to `main`, on
pull requests, on tags/releases and manually.

Jobs:

1. **native-tests** — installs CMake, g++, Stockfish and libcurl, builds the C++
   code, runs `ctest`, then `test_analysis`, `test_server` and
   `scripts/smoke_test.sh` against the real engine.
2. **apk** — installs JDK 17, Android SDK 35, NDK 27.0.12077973 and CMake 3.22.1,
   optionally builds Stockfish for `arm64-v8a` and `x86_64`, then runs
   `gradle -p android :app:assembleDebug`.

Getting the APK:

- **Actions → Build Android APK → latest run → Artifacts → `wechess-debug-apk`**.
- Or publish a release: `gh release create v1.0.0 --generate-notes` — the
  workflow builds and attaches the APK to it. That is the link the landing page's
  "Download the APK" button points at.

Manual run with or without the engine:

```bash
gh workflow run android.yml -f with_stockfish=true
```

Without the engine the APK still installs and the board, PGN import and
navigation work; the review reports that Stockfish was not found. Set
`with_stockfish=false` for that build.

### Signing a release APK

The debug APK is signed with the debug key, which is fine for installing and
testing but not for the Play Store. For a real release key, generate one and pass
the standard Gradle properties (never commit them):

```bash
keytool -genkeypair -v -keystore wechess-release.jks -alias wechess \
        -keyalg RSA -keysize 4096 -validity 10000
cd android
./gradlew :app:assembleRelease \
  -Pandroid.injected.signing.store.file="$PWD/../wechess-release.jks" \
  -Pandroid.injected.signing.store.password=... \
  -Pandroid.injected.signing.key.alias=wechess \
  -Pandroid.injected.signing.key.password=...
```

To do it in CI, base64 the keystore into a repository secret and decode it in a
step, then pass the same `-Pandroid.injected.signing.*` arguments. Keep the
keystore out of the artifact and out of git.

### Stockfish and the GPL

If `with_stockfish=true`, the APK contains GPL-3.0 code. Distributing that APK
means you must also offer the corresponding source — the repository itself plus
the exact engine revision built by the script. If that is not acceptable, publish
builds with `with_stockfish=false` and let users install the engine themselves.

## 4. Vercel

The landing page is `site/`: plain HTML + CSS, no build step and no JavaScript.
`site/vercel.json` adds security headers.

**Important:** the WeChess application cannot be hosted on Vercel. Its entire
backend is a native C++ HTTP server plus a Stockfish subprocess; Vercel runs
static files and serverless functions, not long-lived native processes. So Vercel
hosts the download/documentation page, and the APK is built by GitHub Actions.

### First deployment (CLI)

```bash
npm i -g vercel
cd site
vercel link            # create/select the project, note the org and project ids
vercel deploy --prod   # first production deployment
```

### Automated deployment (workflow)

Workflow: `.github/workflows/vercel.yml`, triggered by changes under `site/` or
manually. Add three repository secrets (Settings → Secrets and variables →
Actions):

| Secret | Where to find it |
| --- | --- |
| `VERCEL_TOKEN` | Vercel → Account Settings → Tokens → Create |
| `VERCEL_ORG_ID` | `site/.vercel/project.json` after `vercel link` (`orgId`) |
| `VERCEL_PROJECT_ID` | same file (`projectId`) |

Then either push a change to `site/` or run:

```bash
gh workflow run vercel.yml
```

Alternatively, skip the workflow entirely: in the Vercel dashboard, "Add New →
Project → Import Git Repository", set **Root Directory** to `site`, framework
preset **Other**, no build command, output directory `.`.

### Custom domain

Vercel → Project → Domains → add your domain and follow the DNS instructions.
Nothing in the app depends on the domain; only the APK download link matters.

## 5. Order of operations

1. `./scripts/package_zip.sh` — archive to hand out.
2. Push to GitHub.
3. Deploy `site/` to Vercel (CLI or the workflow).
4. Let the Android workflow build the APK, then attach it to a release so the
   landing page's download button resolves.

## 6. Troubleshooting

| Symptom | Fix |
| --- | --- |
| `SDK location not found` | Set `ANDROID_HOME`, or create `android/local.properties` with `sdk.dir=...` |
| `NDK not found` / version mismatch | Install `ndk;27.0.12077973` (SDK Manager) or change `ndkVersion` in `android/app/build.gradle.kts` |
| Gradle/AGP incompatibility | Use Gradle 8.7 with AGP 8.5.2; the workflow pins both |
| Stockfish build fails | Check `ANDROID_NDK_HOME`; the script needs `git`, `make` and network access |
| APK starts but says "no engine" | The `jniLibs/<abi>/libstockfish.so` file is missing or not executable; rebuild with `with_stockfish=true` |
| Vercel build fails | The project must point at `site/`, framework preset "Other", no build command |
| Vercel deploy 403/401 in Actions | `VERCEL_TOKEN` expired or the org/project ids belong to another account |
