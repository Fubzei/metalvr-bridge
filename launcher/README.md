MetalVR Bridge
==============

Play Windows Steam games on your Mac.


FOR YOUR BUDDY (the person playing games)
==========================================

1. Unzip "MetalVR-Bridge-Installer.zip"
2. Drag "MetalVR Bridge" to your Applications folder
3. First time only: Right-click the app -> click "Open" -> click "Open" again
   (This is normal, macOS does this for apps not from the App Store)
4. Click "Run Triangle Test" to make sure your Mac works with it
5. Click "Launch Steam" to play your games

That is it. Done.

If the test fails, click "Export" to save the log file and send it back
for troubleshooting.


FOR THE BUILDER (the person compiling this)
=============================================

You need a Mac with Xcode command line tools.
If you do not have them: open Terminal, run "xcode-select --install"

Then:

1. Put these files in one folder:
   - setup.sh
   - MetalVRBridgeApp.swift
   - ContentView.swift
   - BridgeViewModel.swift
   - ProjectStatus.swift
   - CompatibilityCatalog.swift
   - RuntimeLaunchPlan.swift
   - RuntimeBundleManifest.swift
   - RuntimeBundleArtifactPreview.swift

2. Open Terminal, cd to that folder

3. Run:   bash setup.sh

4. Send the resulting "MetalVR-Bridge-Installer.zip" to your buddy

The script automatically finds and bundles the MetalVR Bridge ICD if it
has been built. If `PROJECT_STATUS.json`, `../PROJECT_STATUS.json`, or
`../docs/PROJECT_STATUS.json` is present, it also bundles the current repo
phase snapshot so the launcher can display the next gate and project readiness
status. If `GAME_COMPATIBILITY_CATALOG.json`, `../GAME_COMPATIBILITY_CATALOG.json`,
or `../docs/GAME_COMPATIBILITY_CATALOG.json` is present, it also bundles the
checked-in compatibility snapshot so the launcher can summarize known profiles.
If `launch-plan.json`, `RUNTIME_PLAN_PREVIEW.json`, or their repo-root variants
are present, it also bundles a runtime-plan preview so the launcher can inspect
an exported launch policy without rerunning tools. At runtime, the launcher can
also import either a direct `launch-plan.json` file or a `bundle-manifest.json`
file exported by `scripts/export_runtime_bundle.ps1`. Imported runtime bundles
now also surface their checklist, setup-script, launch-script, lint, and
compatibility-catalog previews in the app. If those files are missing, the app
still works for the Metal hardware test.
