# Tchia Intro Skip

This mod skips Tchia’s intro sequence.\
It skips the startup video, the opening logo cards, and the button prompt and flower-picking sequence.\
As a result, the game goes directly from its usual startup black screen to the title menu without any input.

Supports **Tchia for Windows on Steam, build 14236148**.

[Tchia on Steam](https://store.steampowered.com/app/1496590/Tchia/)

## Install the main edition

1. Close the game. In Steam, open Tchia’s **Manage → Browse local files** menu to find the folder containing `ProjetCaillou.exe`.
2. If you previously installed this mod under `Tchia/Content/Paks/~mods/`, move that copy of `Tchia-IntroSkip_P.pak` outside the game folder. If another mod installed `X3DAudio1_7.dll`, back it up and check which mod needs it before replacing it.
3. Extract the main ZIP into the folder containing `ProjetCaillou.exe`. The files should end up here:

   - `Tchia/Content/Paks/Tchia-IntroSkip_P.pak`
   - `Tchia/Binaries/Win64/X3DAudio1_7.dll`

4. Launch Tchia through Steam.

Keep both files installed together. The DLL lets the Steam build load its original loose assets while a mod pak is active.

**Uninstall:** Close Tchia and remove `Tchia-IntroSkip_P.pak`. Remove the bundled DLL after removing any other paks that rely on it.

## Optional loose-file edition

This is an alternative to the main ZIP. It contains seven loose files and installs by extraction.

1. Close the game. Move the main edition’s pak and DLL outside the game directory. Move any other mod paks out too for a DLL-free setup.
2. Back up these four files outside the game directory, preserving their relative paths:

   - `Tchia/Content/Level/RefLevels/lvl_mainmenu.umap`
   - `Tchia/Content/Level/RefLevels/lvl_mainmenu.uexp`
   - `Tchia/Content/Level/MainMenu/ui_Tchiamainmenu.uasset`
   - `Tchia/Content/Level/MainMenu/ui_Tchiamainmenu.uexp`

3. Extract the optional ZIP beside `ProjetCaillou.exe` and allow those four files to be replaced.

**Uninstall:** Restore the four backups and remove the three files added by the optional ZIP:

- `Tchia/Config/Windows/WindowsGame.ini`
- `Tchia/Content/Level/MainMenu/LaunchCards/IntroSkipCards.uasset`
- `Tchia/Content/Level/MainMenu/LaunchCards/IntroSkipCards.uexp`

The loose-file archive has been checked against the main pak’s contents. In-game verification of the loose-file edition is pending.

## Build from source

Use Python 3.10 or newer, the .NET 8 SDK, UnrealPak from Unreal Engine 4.24, and the Visual Studio x64 C++ build tools. The asset tool uses UAssetAPI 1.1.0. Build against an unmodified Steam installation of build 14236148.

```powershell
dotnet build src/AssetTool -c Release
powershell -ExecutionPolicy Bypass -File src/LoosePakFix/build.ps1
$dll = "dist/LoosePakFix/X3DAudio1_7.dll"
$hash = (Get-FileHash -Algorithm SHA256 $dll).Hash.ToLowerInvariant()
$id = "dev-g" + (git rev-parse --short=7 HEAD)
python tools/build_pak_intro.py --game-root "<folder containing ProjetCaillou.exe>" `
  --asset-tool src/AssetTool/bin/Release/net8.0/AssetTool.dll `
  --unreal-pak "<path to UnrealPak.exe>" --helper $dll --helper-sha256 $hash `
  --build-id $id --output "dist/$id"
```

The script checks the source asset hashes and writes both ZIPs to the output directory.

### Optional compatibility verifier

If you modify the compatibility helper, you can build and run its offline verifier. Use a copy of the original Steam build 14236148 executable.

```powershell
powershell -ExecutionPolicy Bypass -File src/LoosePakFix/build.ps1 -BuildVerifier
& "dist/LoosePakFix/verify.exe" `
  "<copy of ProjetCaillou-Win64-Shipping.exe>" `
  (Resolve-Path "dist/LoosePakFix/X3DAudio1_7.dll").Path
```

The verifier checks the compatibility patch and audio forwarding without launching Tchia. Both install ZIPs omit it.

## Development and verification

OpenAI Codex assisted development. The mod author verified the mod in-game.

All rights in the game and its content remain with their respective rights holders. Third-party components remain the property of their respective owners.
