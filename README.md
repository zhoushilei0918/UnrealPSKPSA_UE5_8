<div align="center">

# Unreal PSK / PSKX / PSA Importer

**在 Unreal Engine 5.8 中直接导入模型与动画**

**简体中文** · [English](README.en.md)

[安装](#安装) · [导入模型](#导入模型) · [导入动画](#导入动画) · [查看结果](#查看结果)

</div>

---

支持 **PSK / PSKX 模型**与 **PSA 动画**，提供朝向选择、无效关键帧修复及日志筛选。已在 Windows / UE 5.8 编辑器中验证。

## 安装

1. 将插件放入项目的 `Plugins/UnrealPSKPSA` 目录，确保其中包含 `UnrealPSKPSA.uplugin`。
2. 使用 UE 5.8 对应的 C++ 编译环境编译项目的 **Editor** 目标；纯蓝图项目可先添加一个 C++ 类。
3. 在 **Edit → Plugins** 中启用 **Unreal PSK / PSKX / PSA Importer**，按提示重启编辑器。

## 导入模型

1. 在 **Content Browser** 中选择目标文件夹，点击 **Import**，或直接拖入 `.psk` / `.pskx`。
2. 设置源模型正面与目标朝向，然后点击 **导入**。默认是 **源 +X → UE +Y**，也可选择 `+X / +Y / -X / -Y`。
3. 检查生成的模型。包含骨骼数据的 PSKX 会按骨骼模型导入。

<p align="center">
  <img src="assets/mesh-orientation.jpg" alt="PSK 与 PSKX 导入时的朝向设置" width="606">
  <br>
  <sub>模型朝向设置：同一角色的身体、头部、服装使用相同选项。</sub>
</p>

> [!TIP]
> PSA 动画会自动沿用模型保存的导入朝向。需要修改朝向时，先重新导入模型，再导入动画。

## 导入动画

### 1. 打开面板

选中一个 **Skeletal Mesh**，右键选择 **导入 PSA 动画…**，目标模型会自动填入。也可从 **Tools → 导入 PSA 动画…** 或工具栏的插件图标打开面板。

<p align="center">
  <img src="assets/psa-context-menu.jpg" alt="从骨骼网格右键菜单打开 PSA 导入面板" width="1000">
  <br>
  <sub>右键骨骼网格，选择“导入 PSA 动画…”。点击图片可查看原图。</sub>
</p>

### 2. 选择文件并导入

确认目标网格与保存路径，来源保持 **自动识别（推荐）**。点击 **选择 PSA 文件…** 或 **从文件夹添加…**，然后点击 **导入动画**。

<p align="center">
  <img src="assets/psa-panel.jpg" alt="选择目标网格、保存路径及 PSA 动画文件" width="766">
  <br>
  <sub>动画导入面板：支持多文件导入，目标网格也可以来自兼容的 FBX。</sub>
</p>

| 常用选项 | 使用说明 |
| :--- | :--- |
| 保存路径 | 填写 UE 内容路径，例如 `/Game/Characters/Animations` |
| 覆盖同名动画 | 默认关闭；开启后仅覆盖使用相同 Skeleton 的同名动画 |
| 修复无效关键帧 | 默认关闭；中间缺帧插值，首尾缺帧复制最近有效帧，整条骨骼轨道无有效帧则失败 |
| 使用模型参考缩放 | 默认关闭；动画异常拉伸时可开启后重新导入，使用目标模型各骨骼的参考缩放，忽略 PSA 缩放效果，保留位置和旋转 |

“从文件夹添加”只读取当前目录，子目录需分别添加。同一文件不会重复加入列表。

## 查看结果

导入后查看成功、失败数量，点击 **显示导入结果** 即可在 Content Browser 中定位动画。日志可按 **错误 / 警告 / 成功** 独立筛选。

<p align="center">
  <img src="assets/psa-result.jpg" alt="实际导入成功后的结果汇总与分类日志" width="766">
  <br>
  <sub>实际导入示例：成功生成 1 个动画，日志同时提示未匹配的骨骼。</sub>
</p>

> [!IMPORTANT]
> 请使用对应角色的模型和动画。骨骼名称相同不代表可以直接通用；不兼容时请检查目标模型。FBX 或旧模型若没有本插件的朝向记录，不会自动补加旋转。

---

基于 [h4lfheart/UnrealPSKPSA](https://github.com/h4lfheart/UnrealPSKPSA) 修改。欢迎提交 Issue 或 Pull Request；反馈时请附 UE 版本、导出工具和错误日志。

<div align="center">

**简体中文** · [Read in English](README.en.md)

</div>
