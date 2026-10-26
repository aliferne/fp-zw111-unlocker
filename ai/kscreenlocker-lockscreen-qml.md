# KScreenLocker 锁屏 QML 结构与加载流程

本文说明 `org.kde.plasma.desktop` shell 包中 `contents/lockscreen/` 各文件的作用，以及这些 QML 如何被 `kscreenlocker_greet` 加载成最终的锁屏界面。

环境：KDE Plasma 6.3.5。
参考源码：`~/desktop/kscreenlocker`（master 与 `v6.3.5` tag 有差异，运行时行为以 `v6.3.5` 为准）。

> `org.kde.plasma.desktop` 不是独立的“锁屏主题包”，而是 **Plasma 的整个 shell 包**。
> 锁屏只是这个 shell 包的一个入口，由 KPackage 的 `Plasma/Shell` 结构定位。

## 1. 目录结构

```text
org.kde.plasma.desktop/
├── metadata.json                 # KPackage 元数据
└── contents/
    ├── lockscreen/
    │   ├── LockScreen.qml        # 锁屏根对象（C++ 加载的入口）
    │   ├── LockScreenUi.qml      # 锁屏主布局/控制器
    │   ├── MainBlock.qml         # 密码框、指纹提示、登录按钮
    │   ├── PasswordSync.qml      # QML singleton，共享密码
    │   ├── NoPasswordUnlock.qml  # 非交互认证成功后的确认页
    │   ├── LockOsd.qml           # OSD 覆盖层
    │   ├── MediaControls.qml     # MPRIS 媒体控制条
    │   ├── config.qml            # 系统设置里的锁屏配置页（KCM）
    │   ├── config.xml            # KConfigXT 配置 schema/default
    │   └── qmldir                # QML 模块清单（声明 PasswordSync）
    ├── osd/                      # LockOsd 用到的通用 OSD 组件
    ├── views/                    # 桌面 shell 的 Desktop/Panel
    └── ...
```

| 文件 | 作用 | 谁加载 |
|---|---|---|
| `metadata.json` | `KPackageStructure: Plasma/Shell`、`KPlugin.Id`、`X-Plasma-APIVersion: 2` | KPackage / `kscreenlocker_greet` |
| `LockScreen.qml` | 锁屏根 `Item`；C++ 通过 `lockscreenmainscript` 加载它 | `QQuickView::setSource()` |
| `LockScreenUi.qml` | 真正的锁屏 UI 主控制器 | `LockScreen.qml` 直接实例化 |
| `MainBlock.qml` | 密码框、用户列表、登录按钮、指纹/智能卡提示、媒体控件 | `LockScreenUi` 的 `StackView.initialItem` |
| `PasswordSync.qml` | 共享密码的 QML singleton | `qmldir` 声明，QML 引擎按需创建 |
| `NoPasswordUnlock.qml` | 指纹等非交互认证成功后的“Unlock”确认页 | `LockScreenUi.onSucceeded` 里 `mainStack.replace` |
| `LockOsd.qml` | 音量/亮度等 OSD，`objectName: "onScreenDisplay"` | `LockScreenUi` 的 `Loader`，`active: root.viewVisible` |
| `MediaControls.qml` | MPRIS 媒体控制 | `MainBlock` 的 `Loader`，`active: config.showMediaControls` |
| `config.qml` | 系统设置“锁屏”页的配置表单，不是运行时 UI | `AppearanceSettings::loadShellConfig()` 给 KCM |
| `config.xml` | 配置项 schema/default（`alwaysShowClock`、`showMediaControls`） | `ShellIntegration::configScheme()`，运行时暴露成 `config` |
| `qmldir` | 目录的 QML 模块清单，声明 `singleton PasswordSync` | QML 引擎 |

## 2. 各文件职责

### `LockScreen.qml`

C++ 加载的入口，也是 QML 和 `kscreenlocker_greet` 之间的“契约对象”。它暴露了 C++ 会按名字查找的属性/信号：

- `locked`：C++ 在 QML 加载后写入，表示是否立即锁屏；
- `viewVisible`：C++ 在第一帧渲染后设为 `true`，`LockOsd` 的 Loader 靠它激活；
- `suspendToRamSupported` / `suspendToDiskSupported`：C++ 写入；
- `suspendToRam()` / `suspendToDisk()`：C++ 连接这些信号来执行挂起/休眠。

它本身只做一件事：

```qml
LockScreenUi { anchors.fill: parent }
```

### `LockScreenUi.qml`

锁屏的“大脑和布局”：

- 最外层 `MouseArea` 负责显示/隐藏锁屏 UI、处理鼠标/键盘活动、10 秒无操作淡出；
- `onUiVisibleChanged` 里调用 `authenticator.startAuthenticating()`，所以锁屏界面出现时密码和指纹会同时开始认证；
- `Connections { target: authenticator }` 处理 `succeeded`、`failed`、`infoMessage`、`errorMessage`、`prompt` 等信号；
- `onSucceeded`：如果 `authenticator.hadPrompt` 为真，直接 `Qt.quit()`；否则切到 `NoPasswordUnlock.qml` 让用户点一下“Unlock”；
- `StackView` 里放 `MainBlock`；
- 底部 `Loader` 加载 `LockOsd.qml`；
- 有 `notification`、`graceLockTimer`（密码失败后 3 秒重试）、`notificationRemoveTimer`（3 秒清通知）等状态。

### `MainBlock.qml`

密码/用户/登录块：

- 基类是 Breeze 的 `SessionManagementScreen`，提供用户头像、用户列表、密码输入区域；
- `passwordBox` 的文本绑定到 `PasswordSync.password`；
- `startLogin()` 发出 `passwordResult(password)`，由 `LockScreenUi` 转成 `authenticator.respond(password)`；
- `FailableLabel` 显示指纹/智能卡的默认提示，并处理 `authenticator.noninteractiveError`（即 `pam_error`）；
- `TestLabel` 是最小实验：监听 `authenticator.noninteractiveInfo`（即 `pam_info`），把文字换成 `authenticator.infoMessage`；
- `Loader { source: "MediaControls.qml" }` 根据 `config.showMediaControls` 决定是否加载媒体控件。

### `PasswordSync.qml`

`qmldir` 里声明的 singleton：

```qml
singleton PasswordSync 1.0 PasswordSync.qml
```

它只存一个 `property string password`。多显示器时每个屏幕都有自己的 `LockScreenUi`/`MainBlock` 实例，但共享同一个 QML 引擎和 singleton，所以在一个屏幕输入密码，其他屏幕的密码框会同步。它也是 fallback 主题和 shell 主题共享密码的方式。

### `NoPasswordUnlock.qml`

当认证成功、但整个过程中没有出现过密码 prompt（`hadPrompt == false`，典型是指纹/智能卡）时，`LockScreenUi` 不直接退出，而是显示这个页面；用户点击“Unlock”后才 `Qt.quit()`。这是 KDE 的 UX/安全选择，避免非交互认证成功后立刻解锁。

### `LockOsd.qml`

OSD 覆盖层。关键点是：

```qml
objectName: "onScreenDisplay"
```

C++ 里 `UnlockApp::osdProgress()` / `osdText()` 会：

```cpp
auto osd = v->rootObject()->findChild<QQuickItem *>("onScreenDisplay");
osd->setProperty("osdValue", ...);
QMetaObject::invokeMethod(osd, "show");
```

这些槽函数连接的是 `org.kde.plasmashell` 的 `org.kde.osdService` D-Bus 信号，所以音量/亮度 OSD 能显示在锁屏上。它内部 import 了 `../osd` 的 `OsdItem`。

### `MediaControls.qml`

用 `org.kde.plasma.private.mpris` 的 `MultiplexerModel` 聚合播放器，显示专辑封面、标题、播放/暂停/上一首/下一首。它不参与认证，只是锁屏上的媒体控件。

### `config.qml` 和 `config.xml`

这两个要分清楚：

- **`config.xml`** 是 KConfigXT schema：定义 `General` 组里的 `alwaysShowClock`、`showMediaControls` 及默认值。`ShellIntegration::configScheme()` 读取它，生成 `KConfigPropertyMap`，作为 QML 的 `config` 上下文属性。运行时 `LockScreenUi` 用 `config.alwaysShowClock`，`MainBlock` 用 `config.showMediaControls`。
- **`config.qml`** 是系统设置里的配置页（KCM 表单），不是锁屏运行时加载的。`AppearanceSettings::loadShellConfig()` 通过 `m_package.fileUrl("lockscreen", "config.qml")` 找到它，交给 System Settings 的“锁屏”KCM 显示。它里面的 `cfg_alwaysShowClock` / `cfg_showMediaControls` 是 KCM 框架的绑定，不是运行时 `config` 对象的属性。

### `metadata.json`

KPackage 靠它识别包类型和版本：

- `KPackageStructure: Plasma/Shell`：告诉 KPackage 这是 shell 包；
- `KPlugin.Id: org.kde.plasma.desktop`：包名，`ShellIntegration::defaultShell()` 默认用它；
- `X-Plasma-APIVersion: 2`：`kscreenlocker_greet` 的 `verifyPackageApi()` 要求 >= 2，否则会退回默认包或内置 fallback。

## 3. 从 C++ 到最终锁屏界面的加载链

1. **ksmserver/ksldapp 触发锁屏**，启动 `kscreenlocker_greet`，并传入 `--graceTime`、`--immediateLock` 等参数。
2. **`UnlockApp` 构造**，创建 `PamAuthenticators`（`kde` + `kde-fingerprint`），加载设置。
3. **选择 shell 包**：
   - `ShellIntegration::defaultShell()` 读取 `plasmashellrc` 的 `[Shell] ShellPackage`，默认 `org.kde.plasma.desktop`；
   - `KPackage::PackageLoader::loadPackage("Plasma/Shell")` + `setPath(shell)`；
   - `verifyPackageApi()` 检查 `X-Plasma-APIVersion >= 2`；
   - `m_mainQmlPath = package.fileUrl("lockscreenmainscript")`，最终指向 `contents/lockscreen/LockScreen.qml`。
4. **加载配置 schema**：`ShellIntegration::configScheme()` 读取 `contents/lockscreen/config.xml`，创建 `config` 属性映射。
5. **每个屏幕创建一个 `QQuickView`**（6.3.5 是共享引擎的 `QuickViewSharedEngine`），设置上下文属性：
   - `kscreenlocker_userName`、`kscreenlocker_userImage`
   - `authenticator`（`PamAuthenticators` 实例）
   - `config`（`KConfigPropertyMap`）
   - `wallpaper`、`defaultToSwitchUser` 等
6. **`view->setSource(LockScreen.qml)`**：QML 引擎解析 `LockScreen.qml`，根据相对路径/import 实例化 `LockScreenUi`、`MainBlock` 等，生成场景图并渲染。
7. **C++ 写回“魔法属性”**：
   - `locked`：是否立即锁屏；
   - `suspendToRamSupported` / `suspendToDiskSupported`；
   - 连接 `suspendToRam()` / `suspendToDisk()` 信号；
   - 第一帧渲染后把 `viewVisible` 设为 `true`，`LockOsd` 的 Loader 随之激活。
8. **认证交互**：
   - `LockScreenUi` 在 UI 可见时调用 `authenticator.startAuthenticating()`；
   - `MainBlock` 把密码通过 `authenticator.respond()` 交给 PAM；
   - PAM 的结果通过 `authenticator.succeeded/failed/infoMessage/errorMessage` 回到 QML；
   - 成功 → `onSucceeded` → `Qt.quit()` 或 `NoPasswordUnlock.qml`；
   - C++ 连接了 `QQmlEngine::quit`，并且只在 `authenticator.isUnlocked()` 为真时才真正退出，防止 QML 绕过认证。
9. **失败兜底**：如果 `LockScreen.qml` 加载失败，`kscreenlocker_greet` 会退回 `qrc:/fallbacktheme/LockScreen.qml`（编译进 kscreenlocker 的极简主题），避免把自己锁在门外。

对象树大致是：

```text
kscreenlocker_greet (C++)
└── QQuickView (每个屏幕一个)
    └── LockScreen.qml
        └── LockScreenUi.qml
            ├── MouseArea / Wallpaper / Clock / Notification
            ├── StackView
            │   └── MainBlock.qml
            │       ├── SessionManagementScreen（密码、用户列表、按钮）
            │       ├── FailableLabel / TestLabel
            │       └── Loader -> MediaControls.qml
            ├── VirtualKeyboardLoader
            ├── Loader -> LockOsd.qml (active: viewVisible)
            └── footer（虚拟键盘、键盘布局、Battery）
```

## 4. C++ 和 QML 的“魔法契约”

`kscreenlocker_greet` 不 import 这些 QML 文件，而是按名字读写根对象：

- 属性：`locked`、`viewVisible`、`suspendToRamSupported`、`suspendToDiskSupported`
- 信号：`suspendToRam()`、`suspendToDisk()`
- `findChild<QQuickItem *>("onScreenDisplay")` 找 `LockOsd`
- 上下文属性：`authenticator`、`config`、`kscreenlocker_userName`、`kscreenlocker_userImage`、`wallpaper` 等
- `import org.kde.kscreenlocker 1.0` 提供 `Authenticator` 枚举和 `Authenticators` 类型（C++ 里用 `qmlRegisterUncreatableType` 注册）

所以改 QML 时，这些名字不要随便改，否则 C++ 找不到，可能表现为锁屏不显示、OSD 不工作、无法挂起等。

## 6. 两个容易踩的点 （我暂时不确定对不对）

- **`pam_info` 会把 `hadPrompt` 置为 true**。6.3.5 的 `PamAuthenticators` 在收到非交互认证器的 `infoMessage` 时会设置 `hadPrompt = true`，于是 `LockScreenUi.onSucceeded` 会直接 `Qt.quit()`，而不是显示 `NoPasswordUnlock.qml`。在 `TestLabel` 里测试 `onNoninteractiveInfo` 时要注意这个副作用。
- **`FailableLabel` / `TestLabel` 的 `visible` 依赖 `authenticator.authenticatorTypes`**。6.3.5 里 fingerprint 的 `isAvailable()` 是 `m_inAuthentication && !m_unavailable`，所以这些标签只在 `pam_authenticate` 还没返回时可见；认证返回后标签会消失。master 的行为不同（`!m_unavailable`），不要拿 master 的源码直接推断 6.3.5 的显示时机。

## 7. 相关源码位置

- `~/desktop/kscreenlocker/greeter/greeterapp.cpp`：`UnlockApp`、`setShell()`、`createViewForScreen()`、OSD 槽函数
- `~/desktop/kscreenlocker/greeter/pamauthenticators.cpp`：`authenticator` 上下文对象、指纹/密码并发认证
- `~/desktop/kscreenlocker/greeter/pamauthenticator.cpp`（v6.3.5）：`PamWorker`、conversation 消息转发
- `~/desktop/kscreenlocker/settings/shell_integration.cpp`：`defaultShell()`、`configScheme()`
- `~/desktop/kscreenlocker/settings/appearancesettings.cpp`：KCM 如何找到 `config.qml`
- `/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/packagestructure/plasma_shell.so`：`Plasma/Shell` 的 `lockscreenmainscript` 映射
- `/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/`：系统当前使用的锁屏 QML
