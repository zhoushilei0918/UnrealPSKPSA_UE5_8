<div align="center">

# Unreal PSK / PSKX / PSA Importer

**Import meshes and animations directly into Unreal Engine 5.8**

[简体中文](README.md) · **English**

[Install](#install) · [Language](#language) · [Import meshes](#import-meshes) · [Import animations](#import-animations) · [View results](#view-results)

</div>

---

Import **PSK / PSKX meshes** and **PSA animations**, with orientation options, invalid key repair, and log filters. Verified in the Windows / UE 5.8 editor.

## Install

1. Place the plugin in your project's `Plugins/UnrealPSKPSA` directory, with `UnrealPSKPSA.uplugin` inside.
2. Build the project's **Editor** target with the C++ toolchain required by UE 5.8. For a Blueprint-only project, first add a C++ class if needed.
3. Enable **Unreal PSK / PSKX / PSA Importer** under **Edit → Plugins**, and restart if prompted.

## Language

The plugin follows the **UE editor language**. Choose **English** or **Chinese (Simplified)** under **Edit → Editor Preferences → General → Region & Language → Editor Language**, then close and reopen the import panel.

Menus, panels, option tooltips, and newly generated import logs are localized. Switching languages does not change orientation or import settings. Keep the plugin's `Content/Localization` folder when installing or distributing it.

## Import meshes

1. Select a destination in the **Content Browser**, then click **Import** or drag in a `.psk` / `.pskx` file.
2. Choose **Source Forward** and **Target Forward**, then click **Import**. The default is **source +X → UE +Y**; `+X / +Y / -X / -Y` are available.
3. Inspect the generated mesh. PSKX files containing skeletal data are imported as skeletal meshes.

<p align="center">
  <img src="assets/mesh-orientation.jpg" alt="PSK and PSKX import orientation settings" width="606">
  <br>
  <sub>Use the same orientation settings for a character's body, head, and clothing.</sub>
</p>

> [!TIP]
> PSA animations inherit the mesh's saved import orientation. To change it, import the model again before importing animations.

## Import animations

### 1. Open the panel

Select one **Skeletal Mesh**, then right-click → **Import PSA Animation…**. The target mesh is filled in automatically. You can also use **Tools → Import PSA Animation…** or the plugin's toolbar icon.

<p align="center">
  <img src="assets/psa-context-menu.jpg" alt="Open the PSA importer from a skeletal mesh context menu" width="1000">
  <br>
  <sub>Right-click a skeletal mesh and choose Import PSA Animation. Click the image for full size.</sub>
</p>

### 2. Choose files and import

Confirm the target mesh and destination, and leave the source on **Auto Detect (Recommended)**. Use **Choose PSA Files…** or **Add from Folder…**, then click **Import Animations**.

<p align="center">
  <img src="assets/psa-panel.en.jpg" alt="English PSA importer with localized options and log filters" width="766">
  <br>
  <sub>The panel supports multiple files and compatible target meshes imported from FBX. Labels follow the editor language.</sub>
</p>

| Common option / UI label | Usage |
| :--- | :--- |
| Destination / 保存路径 | Use a UE content path, such as `/Game/Characters/Animations` |
| Replace existing / 覆盖同名动画 | Off by default; only overwrites same-name animations using the same Skeleton |
| Repair invalid keys / 修复无效关键帧 | Off by default; interpolates interior gaps, copies the nearest valid key at either end, and fails if an entire bone track has no valid keys |
| Use mesh reference scale / 使用模型参考缩放 | Off by default; enable and reimport unusually stretched animations to use each target bone's reference scale. Ignores PSA scale effects while retaining position and rotation |

Add from Folder reads only the selected directory. Add subdirectories separately. Duplicate files are not added twice.

## View results

Check the success and failure counts after importing. Click **Show Imported Assets** to locate animations in the Content Browser. Toggle **Error / Warning / Success** independently to filter the log.

<p align="center">
  <img src="assets/psa-result.jpg" alt="Successful animation import with a result summary and categorized logs" width="766">
  <br>
  <sub>A real import result: 1 animation created, with warnings for unmatched bones.</sub>
</p>

> [!IMPORTANT]
> Use the corresponding character's mesh and animations. Matching bone names alone do not guarantee compatibility; check the target mesh if importing fails. FBX or older meshes without this plugin's orientation metadata receive no additional orientation rotation.

---

Based on [h4lfheart/UnrealPSKPSA](https://github.com/h4lfheart/UnrealPSKPSA). Issues and Pull Requests are welcome. Include your UE version, export tool, and error log when reporting a problem.

<div align="center">

[阅读简体中文版](README.md) · **English**

</div>
