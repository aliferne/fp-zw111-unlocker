# 博客草稿

- title: 为 debian 自制指纹验证功能 —— Linux 系统编程与 PAM 模块开发
- sequence:
  1. ZW111 模块基础功能开发与命令行程序封装
  2. Linux PAM 模块介绍与 PAM 动态库开发
  3. 为 ZW111 打通 “任督 PAM 脉”

# ZW111 模块开发

TODO:

# PAM 模块

需要注意，自己写 PAM 模块一定绝对务必要处理好 PAM 模块的状态码，千万不要一个 `(void)` 过去直接给人家忽略掉，不然你的电脑安全风险会非常非常高！

## PAM 基本流程和简易模块开发

网上看到的简中 PAM 模块教程基本上都是只有配置文件的，但是我们要写 ZW111 的指纹识别模块的话需要串起配置文件和模块开发的流程，因此需要借助一手资料，下面已经列举出了。

---

PAM 模块整体的工作流程：

PAM 应用程序指定对应的 service --> 应用程序发起 `auth` 请求 --> libpam 在 service 内部配置找到对应 so 文件/模块 --> `dlopen` 打开 so 文件并调用对应函数 --> 处理结果返回 libpam 并进行下一步处理 --> PAM 程序收到 API 的返回码并进行对应的处理

pam service 文件不带 .service 后缀还是挺神奇的，不知道是不是只有 systemd 会带这种后缀，此外该配置文件是 DSL，有一定的语法规则，由于配置文件的相关资料很多，这里只对需要用到的做简要说明。

基本语法规则： `<type> <flag> <path> <args>`
比如这个例子： `auth required xxx.so`，就表示 authentication （用户身份验证） 要求使用 `xxx.so` 这个模块， `xxx.so` 需要实现一些 PAM 模块要求的函数，然后通过返回 `PAM_*` 状态码告知是验证通过还是失败。

我们会用到的只有 auth, required 和 sufficient. auth 就是鉴权用的，验证你是不是对应的用户， required 和另一个叫 requisite 的很像， required 表示调用所有需要的模块，即使有模块失败了也先不管，等整体调用完了再返回失败，而另一个则是一失败就立马返回， sufficient 表示只要验证通过就直接返回成功，并且自动忽略前面模块的 fail。由于 sufficient 有成功就直接返回的特性，我并不建议使用它，接下来我只会在测试中使用。

---

下面是具体的实验方法

首先需要先安装 libpam：

```bash
sudo apt install libpam-dev -y
```

编写 [pam_auth_test.c](./pam_auth_test.c) 文件，
注意一下实际用的时候不建议直接明文将密码等放进去，调 gdb 或者 ltrace 很容易就能破解出来：

```bash
> ltrace -e strcmp ./test_pam pam-auth-test ss ss 2>&1 | grep "pam_auth_test.so"
pam_auth_test.so->strcmp("ss", "test", 4)       = -1
```

然后一眼就知道密码是什么了，再不济用：

```bash
> readelf -p .rodata pam_auth_test.so

String dump of section '.rodata':
  [     0]  [pam_testauth] pam_sm_authenticate called\n
  [    2b]  Username:
  [    38]  [pam_testauth] pam_get_user failed: %d\n
  [    60]  Password:
  [    70]  [pam_testauth] pam_get_authtok failed: %d\n
  [    a0]  [pam_testauth] got user='%s' pass='%s'\n
  [    c8]  testusr
  [    d0]  testpwd
  [    d8]  [pam_testauth] MATCH -> PAM_SUCCESS\n
  [   100]  [pam_testauth] MISMATCH -> PAM_AUTH_ERR\n
```

或者反汇编啥的，硬猜也能猜出点东西出来

然后编译，注意需要编译成共享库：

```bash
gcc -fPIC -shared -o pam_auth_test.so pam_auth_test.c
```

TODO: 需要审查 fPIC 的讲解

fPIC 标志表示需要生成位置无关的代码，因为我们要编译出来的是共享库（即动态链接库），因此自然不可以是位置有关的，如果位置有关，那么这个库就会对外部编译产物有地址位置上的要求，这违背了共享和动态链接的要求 —— 加载一份共享库，所有依赖该库的都能直接调用，不需要将其复制到可执行文件上，更详细的可以去看其他大佬的介绍或者看 CSAPP 第七章关于链接的介绍。

接下来将 so 链接到对应的位置，我这里在 `/lib/x86_64-linux-gnu/security` 中

```bash
# 测完之后记得删掉
sudo ln $(pwd)/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

注意链接最好用硬链接，只要你的盘在同一个文件系统内就行，硬链接的好处在于两个文件都指向了同一 inode 且修改任何一个文件的内容，其他文件都会跟着更新，删除任意文件都不会导致已有的文件失效，除非全删光。

然后编写服务文件

```bash
tee pam-auth-test << EOF
auth required pam_auth_test.so
account required pam_auth_test.so
EOF
```

最后链接这个服务文件

```bash
# 测完之后记得删掉
sudo ln $(pwd)/pam-auth-test /etc/pam.d
```

这样子通路就基本搭建完毕了，我们接下来自己写一个[应用程序](./pam_auth_test.c)

然后编译：

```bash
gcc -o pam_auth_test.c test_pam -lpam
```

接下来运行：

```bash
> ./test_pam pam-auth-test testusr testpwd
[pam_testauth] pam_sm_authenticate called
[test_pam] here
[test_pam] msg: Password:
[pam_testauth] got user='testusr' pass='testpwd'
[pam_testauth] MATCH -> PAM_SUCCESS
pam_authenticate -> 0 (Success)
```

当然也可以用 `pamtester` 去测试：

```bash
> pamtester -v pam-auth-test testusr authenticate
pamtester: invoking pam_start(pam-auth-test, testusr, ...)
pamtester: performing operation - authenticate
[pam_testauth] pam_sm_authenticate called
Password:
[pam_testauth] got user='testusr' pass='testpwd'
[pam_testauth] MATCH -> PAM_SUCCESS
pamtester: successfully authenticated
```

PAM 模块学习草稿基本写这么简单就行，后面写博客再扩充

## PAM 和 KDE 的联动

我的桌面环境是 KDE Plasma 6：

```bash
> fastfetch | grep "DE:" # 不要在意左半部分， fastfetch 会画 ASCII 图的
 Y$$.    `.`"Y$$$$P"'          DE: KDE Plasma 6.3.6
```

KDE 本身自带一个锁屏登录模块，而刚开机时则一般由 SDDM 或者其他的 Display Manager 接管，这里不详细讨论这个问题，只要知道它是独立于 Desktop Environment 的一个应用，用于在 shell 日志滚完之后接管其显示，启动顺序是 shell -> DM -> DE， KDE 一般和 SDDM 搭配使用，在 SDDM 验证身份之后，由 KDE 接替接下来的显示工作（占用一个新的 tty），并且之后也是 KDE 来负责会话的管理（锁屏，解锁，切换用户等）

KDE 和 SDDM 各自会占用一个 tty， SDDM 在验证通过之后你再切过去 (ctrl + alt + fx, x 是多少自己试一下就知道了) 会发现是卡死的，不过这点对于我们接下来要做的事来说不重要。

首先还是老规矩，简单编写一个[模块](./pam_kde_auth_test.c)并链接过去，然后测试一下

```bash
# 这里测完之后记得删掉
> gcc -fPIC -shared -o pam_kde_auth_test.so pam_kde_auth_test.c && sudo ln $(pwd)/pam_kde_auth_test.so /lib/x86_64-linux-gnu/security
```

注意， KDE 的 PAM 服务文件在 `/usr/lib/pam.d` 里面，你 `ls` 之后会发现有三个 kde 相关的，分别对应三种解锁方式：密码，指纹，卡片，这三个验证方式是独立的，任意一个验证成功都能解锁，而我们之前一直在讲的都是 `/etc/pam.d` 里面的文件，那么这里就有个配置文件查询优先级的问题，一般是 `/etc/pam.d` 里面的优先调用，我们接下来修改 `kde-fingerprint` 即可，其他的别动：

```plain text
# ...
auth	  required	      pam_fprintd.so
auth    required        pam_kde_auth_test.so  # 这个是我们添加的
# ...
```

然后借助 `pamtester`， 我们来调用下这个服务测试一下：

```bash
> sudo pamtester -v kde-fingerprint $USER authenticate
pamtester: invoking pam_start(kde-fingerprint, ferne, ...)
pamtester: performing operation - authenticate
[kde_authtest] called
Password: ^C

>
```

接下来我们验证下锁屏是否能触发，锁屏之后再解锁，日志会出现如下内容：

```bash
> sudo journalctl -f | grep -i kde_auth
10月 19 20:50:42 fernelaptop kwin_wayland_wrapper[219614]: [kde_authtest] called
```

因此解锁这个过程肯定会和 kde-fingerprint 这个服务有点关系，尽管这里写的是 `kwin_wayland_wrapper` 的日志

我们这样子只是验证了这样子配置是 OK 的，自己编写的模块会被调用，而现在我们需要的是要自己定制一下，我之前提到了配置文件优先级的一个问题，现在我们把东西复制过去：

```bash
> cp /usr/lib/pam.d/kde-fingerprint /etc/pam.d/kde-fingerprint
```

然后还原在 `/usr/lib/pam.d/kde-fingerprint` ，再打开刚复制过去的文件，把上面自己写的那个配置项的 `<flag>` 从 required 改成 sufficient，然后你锁屏后再打开，就会发现不需要输入密码了。
接下来我会建议注释原来的 `pam_fprintd.so` 选项，改用自己自定义的 `.so` 作为指纹模块：

```plain text
#auth	  required	      pam_fprintd.so
auth    required        pam_fprint_zw111.so  # 这个是我们添加的
```

新建 `pam_fprint_zw111.c` 并编写模块交互代码。

```bash
> gcc -fPIC -shared -o pam_fprint_zw111.so pam_fprint_zw111.c && sudo ln $(pwd)/pam_fprint_zw111.so /lib/x86_64-linux-gnu/security
```

然后测试，发现可以直接解锁，然后我们只要对接写好的指纹模块即可

# 打通 “任督 PAM 脉”

需要固定设备名称，不然很不方便，udev 规则，这里直接放博客链接，不详细写

```bash
> udevadm info -a -n /dev/ttyUSB1
...
looking at device '/devices/pci0000:00/0000:00:08.1/0000:06:00.4/usb3/3-1/3-1.2/3-1.2:1.0/ttyUSB1/tty/t>
  KERNEL=="ttyUSB1"
  # 需要注意下下面写 udev rules 时应该写 usb, 这里不知道为什么是 tty，可能是我连了扩展坞导致的
  SUBSYSTEM=="tty"
  DRIVER==""
...
```

然后在 `/etc/udev/rules.d/` 创建文件 `fprint_zw111.rules` 并写入：`KERNELS=="3-1.2:1.0", SUBSYSTEMS=="usb", MODE:="0666", SYMLINK+="FP_ZW111"`

最后再 `sudo udevadm control --reload-rules && sudo udevadm trigger` 以使得规则生效

然后就会得到：

```bash
> ls /dev/FP_ZW111
/dev/FP_ZW111
```

此时我们完成了固定命名的操作，可以更加愉快地开发了。

然后我们不再使用锁屏去测试，而是继续使用 `pamtester`：

```bash
> sudo pamtester -v kde-fingerprint $USER authenticate
pamtester: invoking pam_start(kde-fingerprint, ferne, ...)
pamtester: performing operation - authenticate
23:09:41 INFO  src/pam_fprint_zw111.c:38: pam_sm_authenticate called
23:09:41 INFO  src/pam_fprint_zw111.c:40: now openning device /dev/FP_ZW111...
23:09:41 INFO  src/pam_fprint_zw111.c:51: device /dev/FP_ZW111 opened, now configuring terminal parameters...
23:09:41 DEBUG src/serial.c:63: request from fd: 3, set baudrate: 4097, databit: 3, stopbit: 0, parity: 0, vtime: 0, vmin: 0

23:09:41 INFO  src/protocol.c:289: ====================================================
23:09:41 DEBUG src/protocol.c:99: fill pack: id=1 code=32 data_len=5 total_len=17 checksum=0240
send:         EF 01 FF FF FF FF 01 00 08 32 02 FF FF 00 05 02 40
23:09:41 INFO  src/protocol.c:128: write 17 bytes
recv:         EF 01 FF FF FF FF 07 00 08 00 05 00 01 07 BB 00 D7
23:09:42 DEBUG src/protocol.c:194: nbr: 17, p_recv->code: 0x00  # 此处是我把手指放上去了
23:09:42 INFO  src/protocol.c:309: identified!
23:09:42 INFO  src/protocol.c:323: ====================================================
Password: # 直接按 enter 就行
pamtester: successfully authenticated
```

不过后面我们依然需要锁屏测试，那么怎么看日志呢？很简单，借助 `ctrl+alt+fx` 就能打开任意 tty 界面（不过需要注意 KDE 和 SDDM 本身会占用 tty, 不要开到这两个），然后登录一下，输入 `sudo journalctl -f | grep kwin_wayland_wrapper` 并确认，此时这个 tty 界面会一直显示该服务相关内容，我们就可以对 ZW111 模块的情况进行监视了。

创建定时器
注意 `clock_gettime` 需要 gnu99，即需要定义 POSIX 标准，不能是 STRICT_ANSI 模式

`/lib/x86_64-linux-gnu/libexec/kscreenlocker_greet --testing`

稍稍改了一下之前 AI 生成的 cli.c，有很多功能没完善，但是雏形有了，已经很方便了

查了一下发现可以通过 `pam_error` 来让锁屏界面显示一些提示信息
于是 `pam_error(pamh, "你还有 %d 次重试机会", MAX_RETRY - retry);`
但是发现会卡窗口期，如果验证失败之后被弹跳动效搞了的话，就会导致提示信息显示不及时
我们发现锁屏测试的时候指纹验证不通过的动效

找到了 `/usr/share/plasma/shells/org.kde.plasma.desktop/`，里面的 QML 和锁屏界面有关，看到 lockscreen 目录，可以修改这里的进行配置，clone 了 kscreenlocker 的源码进行查看，发现使用了 PAM 的对话机制 `PamAuthenticator::converse( ... )`， `Q_EMIT` 会发送 `pam_info` 和 `pam_error` 发出去的 `msg` 给到 QML, 但是 QML 只会显示 `pam_error` 的 `msg`， 原因在于只有这个是 `onNoninteractiveError` 的，我们需要扩充一下。

简单学习一下 QML， Qt 之间的机制，然后改动 QML，新增:

```qml
// File: .../lockscreen/MainBlock.qml
component FPLabel : PlasmaComponents3.Label {
    id: _fpLabel
    required property int kind
    required property string label

    visible: authenticator.authenticatorTypes & kind
    text: label
    textFormat: Text.PlainText
    horizontalAlignment: Text.AlignHCenter
    Layout.fillWidth: true

    Connections {
        target: authenticator

        function onNoninteractiveInfo(kind, authenticator) {
            if (kind & _fpLabel.kind) {
                _fpLabel.text = Qt.binding(() => authenticator.infoMessage)
           }
       }
    }
}

FPLabel {
    kind: ScreenLocker.Authenticator.Fingerprint
    label: "(此处显示与指纹模块 ZW111 有关的信息)"
}
```

然后可以显示 `pam_info` 给出的东西，并且完全避免我不想要的失败动效
当然如果想要创建一个新 Label 的话，就是复制一下 `FailableLabel` 并粘贴，再删掉多余部分（Timer, 动画等），最后声明一下这个组件就行（即最后补上 `NewLabel { ... }`）。

qml 之间似乎通过一些规约互相引用，需要了解一下
这里应该只会改动 `MainBlock.qml`

由于是声明式的，所以只需要把组件声明之后放在那里就行，显示顺序取决于声明顺序

---
comments: 这部分是当时代码里面新加了 `TestLabel` 的片段，酌情考虑是否加入博客中
          现在需要加入了，我觉得还是新建一个标签好一些

然后发现 label 无法汉化，
查询 i18nd => GNU gettext & po/mo 文件
=> `i18nd( "<filename>.mo", "<msgid>" )`
=> `msgunfmt /usr/share/locale/zh_CN/LC_MESSAGES/plasma_shell_org.kde.plasma.desktop.mo -o ./plasmashell.po`
=> find `Unlock`
=> 找到对应汉化文本：解锁

如果要使用 `i18nd`，那么就是把 plasmashell.po 给改一下加点内容，然后编译成 plasma_shell_org.kde.plasma.desktop.mo 并丢到 `~/.local/share/locale/zh_CN/LC_MESSAGES/` 里面就行
如果不想做那么标准的国际化操作，直接写文字就行。

---

需要考虑对 fd 上锁，如果只用于 kde-fingerprint 就还好，但是如果用到 sudo 等程序中就会存在多进程的竞态条件


# 参考资料（TODO: 还需要从 README 中补充一些过来）

1. https://zhuanlan.zhihu.com/p/663757925
2. https://wiki.archlinux.org.cn/title/PAM
3. [flag fPIC]: https://zhuanlan.zhihu.com/p/368364069
   同时 fPIC 选项可以参考 CSAPP 的第七章
4. [gettext, msg(un)fmt command]: https://www.cnblogs.com/jjxhp/p/12036187.html

下面这些是一手资料，质量更好：

1. http://www.fifi.org/doc/libpam-doc/html
2. Linux man page of pam (`man pam`)
3. [pamtester]: https://pamtester.sourceforge.net/
