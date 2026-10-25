# Steam 外观：用户确认的预装基准（2026-10-10）

## 用户确认和修改边界

- 用户确认缓存后已稳定，不再追查闪烁；不要因上一轮的冲突推测擅自停用或修改主题。
- 当前这套 7 项主题及其参数就是 YMCC 的预装/恢复预设基准。用户未要求重新设计参数。
- 继续沿用“复制文件夹 + 原安装预设 BAT”的方式，不改安装 BAT，不改 Steam 大屏启动 BAT。
- 不复制开发机的运行缓存、日志和接管/恢复记录到别人的电脑。

## 已核对的同一份配置

源码基准位置：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\PowerControl\steam-appearance\themes

当前用户的 61 个主题资源/配置与源码、本机 YMCC 自带主题、交付目录逐文件 SHA-256 一致。
7 项全部启用；配置键 Brighntess 是作者原有拼写，不应擅自改成 Brightness。
完整参数和资源摘要由源码基准目录上一级的 manifest.json 固定；不能只复制 active 开关而遗漏参数文件。

```json
[
  {
    "name": "Art Hero",
    "version": "v3.2.03",
    "config": {
      "active": true,
      "Footer Options": "Smaller Footer",
      "Gradient Settings": "Linear Gradient - Inverted",
      "Hero Appearance": "Expanded Hero",
      "Hero Alignment": "Center - Default",
      "Hero Fit": "Cover - Default",
      "Hero Z-Order": "Default",
      "Hero Height": "0",
      "Hero Height - Fine Control": "0",
      "Header Bar Height": "0",
      "Header Bar Height - Fine Control": "0",
      "No Header": "No",
      "Carousel Font Size": "No Change",
      "Carousel Font Height": "No Change",
      "Carousel Height": "4",
      "Carousel Height - Fine Control": "4"
    }
  },
  {
    "name": "Centered Game Text",
    "version": "v1.1",
    "config": {
      "active": true
    }
  },
  {
    "name": "Darken Unfocused Games",
    "version": "v1.1",
    "config": {
      "active": true,
      "Brighntess": "0.5",
      "Saturation": "0.5",
      "Apply": "Globally"
    }
  },
  {
    "name": "Game Cover Reflections",
    "version": "v1.0",
    "config": {
      "active": true,
      "Focused Opacity": "0.1",
      "Unfocused Opacity": "0.3",
      "Blur": "1",
      "Opacity Smoothness": "0.6"
    }
  },
  {
    "name": "Mini Carousel",
    "version": "v1.0",
    "config": {
      "active": true,
      "Size": "0.7"
    }
  },
  {
    "name": "NEX-85 Keyboard",
    "version": "v2.1",
    "config": {
      "active": true,
      "Dark Mode": "Yes",
      "Focus Colour": "Default"
    }
  },
  {
    "name": "Round",
    "version": "v2.7",
    "config": {
      "active": true,
      "Intensity": "10px",
      "Outlines": "Round"
    }
  }
]
```

## 旧复制预装的问题与最小修正

C:\SOFT\CSSLoader Desktop\安装预设.bat 原样不动：从 C:\SOFT\CSSLoader Desktop\homebrew 复制到运行 BAT 的用户的 %USERPROFILE%\homebrew，继续使用 /E /I /Y /Q。
旧模板仅有 Art Hero、Custom Loader、handheld-controller-glyphs、NEX-85 Keyboard。其中 Art Hero 的 Footer/Gradient 为旧参数，键盘也仍为浅色，另外 5 项目标主题缺失。直接复制旧模板不能复现当前效果。
本次只替换预装主题数据，新模板恰好为 7 个主题、61 个基准文件。旧模板完整保留在：
C:\SOFT\CSSLoader Desktop\YMCC-Preset-Backup-20261010-221638-d57385a6
旧模板未启用的 Custom Loader 和 handheld-controller-glyphs 只留在备份，不混入新预装。

## 换电脑时仍然直接复制

1. 沿用 C:\SOFT\CSSLoader Desktop，保留已有 CSSLoader Desktop.exe 和 CssLoader-Standalone-Headless.exe。此专项交付仅附主题数据及原安装 BAT，不是 CSSLoader 完整安装包。
2. 交付中的 CSSLoader Desktop\homebrew 可直接合并到另一台电脑对应目录。在目标用户登录后执行原安装预设 BAT；不要在制作者或 SYSTEM 账户下执行后假设其它用户也已安装。
3. BAT 的目标是 %USERPROFILE%，没有写死 DaVe；主题资源没有开发机绝对路径，用户名变化不需要修改主题内容。
4. 干净预装可直接套用这 7 项完整参数；CSSLoader/Steam 大屏仍按原联动方式启动，不换启动 BAT。
5. 原 xcopy 只覆盖同名文件，不会停用对方已有其它主题。已有配置的电脑，在 YMCC 中“重新应用”走现有备份/优先套用逻辑；不要为了套用预设递归删除对方全部 themes。
6. 不克隆本机 %LOCALAPPDATA%\YMCC\SteamAppearance 的接管记录。css_translations.json、standalone.log、STORE 不作为预装基准分发。
7. 普通分项切换/资源修复继续保留用户参数；只有明确“恢复 YMCC 预设”才还原本文参数，不能每次启动覆盖用户选择。

## 验证与限制

- 当前用户、源码、本机 YMCC、专项 PowerControl 交付、CSSLoader 预装模板：61 项逐字节一致。
- 新增可直接合并的 CSSLoader 预装交付：61 项摘要一致。
- 实际发布过滤函数包含 61 项资源 + manifest + preset 模块 + 主 helper，共 64 项。
- 隔离的第二用户、坏配置、恢复/回滚等测试：29 + 33 = 62 项通过。
- 原 xcopy 参数在带空格和中文用户名的隔离目录完成复制，61 项与基准一致。
- 安装 BAT、源码/本机 Steam 启动 BAT 摘要不变；没有刷新/重启真实 Steam/CSSLoader，没有修改当前用户的主题参数。
- 此结果确认文件和用户路径可移植，不代表已在另一台实体电脑验证所有 Steam/CSSLoader 版本；建议沿用同一已验证的 CSSLoader 程序版本。

完整检查记录：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\validation\portability\baseline-portability-audit.json