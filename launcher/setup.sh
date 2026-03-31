#!/bin/bash
# =============================================================================
# MetalVR Bridge - One-Command Build
# =============================================================================
#
# WHAT THIS DOES:
#   Creates a ready-to-distribute "MetalVR Bridge.app" that anyone can use.
#   Your buddy just unzips it, drags to Applications, and opens it. That's it.
#
# REQUIREMENTS:
#   - A Mac running macOS 13+ with Xcode command line tools installed
#   - If you don't have Xcode tools: run "xcode-select --install" first
#
# USAGE:
#   1. Put all the .swift files and this script in the same folder
#   2. Open Terminal
#   3. cd to that folder
#   4. Run: bash setup.sh
#   5. Send the resulting .zip to your buddy
#
# =============================================================================

set -e

echo ""
echo "  ============================================"
echo "          MetalVR Bridge - Builder"
echo "     Building your app... sit tight."
echo "  ============================================"
echo ""

# Check for Xcode tools
if ! command -v swiftc &> /dev/null; then
    echo "ERROR: Swift compiler not found."
    echo "Fix:   Run 'xcode-select --install' and try again."
    exit 1
fi

# Check for required source files
MISSING=0
for f in MetalVRBridgeApp.swift ContentView.swift BridgeViewModel.swift ProjectStatus.swift CompatibilityCatalog.swift RuntimeLaunchPlan.swift; do
    if [ ! -f "$f" ]; then
        echo "ERROR: Missing $f - make sure all .swift files are in this folder."
        MISSING=1
    fi
done
if [ $MISSING -eq 1 ]; then exit 1; fi

echo "[1/6] Compiling app..."

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    TARGET="arm64-apple-macos13.0"
else
    TARGET="x86_64-apple-macos13.0"
fi

swiftc \
    -o MetalVRBridge \
    -framework SwiftUI \
    -framework Metal \
    -framework AppKit \
    -framework CoreGraphics \
    -target "$TARGET" \
    -O \
    -whole-module-optimization \
    MetalVRBridgeApp.swift \
    ContentView.swift \
    BridgeViewModel.swift \
    ProjectStatus.swift \
    CompatibilityCatalog.swift \
    RuntimeLaunchPlan.swift \
    2>&1

echo "[2/6] Creating app bundle..."

APP="MetalVR Bridge.app"
rm -rf "$APP"

mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"
mkdir -p "$APP/Contents/Frameworks"

mv MetalVRBridge "$APP/Contents/MacOS/"

echo "[3/6] Writing app metadata..."

cat > "$APP/Contents/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>MetalVR Bridge</string>
    <key>CFBundleDisplayName</key>
    <string>MetalVR Bridge</string>
    <key>CFBundleIdentifier</key>
    <string>com.metalvr.bridge</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>MetalVRBridge</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>NSHumanReadableCopyright</key>
    <string>MetalVR Bridge</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
</dict>
</plist>
PLIST

echo "[4/6] Bundling ICD (if available)..."

# Auto-detect ICD files in common locations
ICD_DYLIB=""
ICD_JSON=""

SEARCH_PATHS=(
    "."
    ".."
    "../metalvr-bridge"
    "../metalvr-bridge/build"
    "../metalvr-bridge/build/Release"
    "../metalvr-bridge/build/Debug"
    "$HOME/.local/share/vulkan/icd.d"
    "/usr/local/share/vulkan/icd.d"
    "/opt/homebrew/share/vulkan/icd.d"
)

for dir in "${SEARCH_PATHS[@]}"; do
    if [ -z "$ICD_DYLIB" ] && [ -f "$dir/libMetalVRBridge.dylib" ]; then
        ICD_DYLIB="$dir/libMetalVRBridge.dylib"
    fi
    if [ -z "$ICD_JSON" ] && [ -f "$dir/vulkan_icd.json" ]; then
        ICD_JSON="$dir/vulkan_icd.json"
    fi
done

if [ -n "$ICD_DYLIB" ]; then
    cp "$ICD_DYLIB" "$APP/Contents/Frameworks/"
    echo "  Bundled ICD dylib from: $ICD_DYLIB"
else
    echo "  ICD dylib not found (app will still work for the Metal test)"
fi

if [ -n "$ICD_JSON" ]; then
    # Rewrite the manifest to point to the bundled dylib location
    cat > "$APP/Contents/Resources/vulkan_icd.json" << 'MANIFEST'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../Frameworks/libMetalVRBridge.dylib",
        "api_version": "1.3.0",
        "is_portability_driver": true
    }
}
MANIFEST
    echo "  Bundled ICD manifest (rewritten with relative path)"
else
    echo "  ICD manifest not found (app will still work for the Metal test)"
fi

PROJECT_STATUS_SOURCE=""
for path in "./PROJECT_STATUS.json" "../PROJECT_STATUS.json" "../docs/PROJECT_STATUS.json"; do
    if [ -z "$PROJECT_STATUS_SOURCE" ] && [ -f "$path" ]; then
        PROJECT_STATUS_SOURCE="$path"
    fi
done

if [ -n "$PROJECT_STATUS_SOURCE" ]; then
    cp "$PROJECT_STATUS_SOURCE" "$APP/Contents/Resources/PROJECT_STATUS.json"
    echo "  Bundled project status snapshot from: $PROJECT_STATUS_SOURCE"
else
    echo "  Project status snapshot not found (launcher status card will be limited)"
fi

CATALOG_SOURCE=""
for path in "./GAME_COMPATIBILITY_CATALOG.json" "../GAME_COMPATIBILITY_CATALOG.json" "../docs/GAME_COMPATIBILITY_CATALOG.json"; do
    if [ -z "$CATALOG_SOURCE" ] && [ -f "$path" ]; then
        CATALOG_SOURCE="$path"
    fi
done

if [ -n "$CATALOG_SOURCE" ]; then
    cp "$CATALOG_SOURCE" "$APP/Contents/Resources/GAME_COMPATIBILITY_CATALOG.json"
    echo "  Bundled compatibility catalog snapshot from: $CATALOG_SOURCE"
else
    echo "  Compatibility catalog snapshot not found (launcher catalog summary will be limited)"
fi

RUNTIME_PLAN_SOURCE=""
for path in "./launch-plan.json" "./RUNTIME_PLAN_PREVIEW.json" "../launch-plan.json" "../RUNTIME_PLAN_PREVIEW.json"; do
    if [ -z "$RUNTIME_PLAN_SOURCE" ] && [ -f "$path" ]; then
        RUNTIME_PLAN_SOURCE="$path"
    fi
done

if [ -n "$RUNTIME_PLAN_SOURCE" ]; then
    cp "$RUNTIME_PLAN_SOURCE" "$APP/Contents/Resources/launch-plan.json"
    echo "  Bundled runtime plan preview from: $RUNTIME_PLAN_SOURCE"
else
    echo "  Runtime plan preview not found (launcher plan card can still import JSON at runtime)"
fi

echo "[5/6] Creating distributable zip..."

ZIP_NAME="MetalVR-Bridge-Installer.zip"
rm -f "$ZIP_NAME"
ditto -c -k --keepParent "$APP" "$ZIP_NAME"

echo "[6/6] Cleaning up..."
# Keep the .app around for local testing

SIZE=$(du -sh "$ZIP_NAME" | cut -f1)
APP_SIZE=$(du -sh "$APP" | cut -f1)

echo ""
echo "  ============================================"
echo "             BUILD COMPLETE!"
echo "  ============================================"
echo ""
echo "  App:  $APP ($APP_SIZE)"
echo "  Zip:  $ZIP_NAME ($SIZE)"
echo ""
echo "  --------------------------------------------"
echo "    SEND THIS TO YOUR BUDDY:"
echo ""
echo "    $ZIP_NAME"
echo ""
echo "    THEIR STEPS:"
echo "    1. Unzip the file"
echo "    2. Drag 'MetalVR Bridge' to Applications"
echo "    3. Right-click -> Open (first time)"
echo "    4. Click 'Run Triangle Test'"
echo "    5. Click 'Launch Steam' to play games"
echo "  --------------------------------------------"
echo ""
