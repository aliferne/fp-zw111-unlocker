# 博客草稿

- title: 为 debian 自制指纹验证功能 —— Linux 系统编程与 PAM 模块开发
- sequence:
  1. ZW111 模块基础功能开发与命令行程序封装
  2. Linux PAM 模块介绍与 PAM 动态库开发
  3. 为 ZW111 打通 “任督 PAM 脉”

上半部分直接展示最终成果，少量的迭代过程的思考，下半部分则讲一下和 AI 结合的开发过程，以及对整体架构的把控之类的

# 前言

这个项目有点个人小玩具的色彩（笑），我的电脑是天选 5 Pro, 没有指纹识别模块，而我自己又主要用 Debian, 当时是和朋友去福州旅游，在闽都 THO 晚上的小型演唱会现场听 DJ 打碟感到无聊，然后思绪就开始发散，**想着说每次解锁电脑都要输入密码，实在是太麻烦了，因此想搞个指纹模块来玩**，于是就有了这篇博客。当然可能有人会觉得我闲得慌，也确实是闲得慌，我要是不折腾，你们看什么啊？ ~~是啊，看什么?~~

注意上面的需求，因为这个需求后面给我扩展了一下，变成了接入任意 PAM 服务，也导致了一些架构上的调整，我会在第二篇文章讲述自己的 “解题思路” 和踩的一些坑，同时我也想在第二篇讲讲我自己对 AI Coding 的一些想法。

指纹模块相关代码已经[开源][项目开源链接]， 主要逻辑部分基本完全古法编程，而审(zhao)查(cha)工作主要由 DeepSeek V4.1 Flash 负责，**这篇博客也是纯手敲**。

这个需求我个人感觉挺简单的，其实可以直接让 v4.1 vibe 一个出来，但是我是当 recreational programming 来玩的 (幻视 tsoding)，所以没有完全 AI 开发，但却没想到这玩意写了我快一个月……（虽然我每天只有大约两三个小时投入开发就是了）。

这里需要提一嘴，我是 Linux 系统编程的初学者，如下文有误，还请不吝赐教。

# 项目架构

因此一开始是硬件的调研，我想知道有什么指纹模组可以给我开发，于是问了 deepseek 之后决定搞个 ZW111 模块来玩，这玩意加上一个 USB 转 TTL 也就 30 块，市面上的 Windows Hello 模组大概是四五十到一百都有。

然后拿到数据手册，脑子里面便大概有了一些开发的方向，我个人角度上认为这一部分主要有 4 个递进的任务： Linux 串口配置(`termios.h`) -> Linux 串口通信(`fcntl.h` 等) -> 实现数据手册规定协议 -> 测试功能

我们需要了解一下整个任务涉及到的部分：

Linux 串口配置 -> 与 ZW111 的通信 -> 开发属于 ZW111 的 PAM 模块 -> 配置关键文件，使 libpam 能够与自己的 PAM 模块交互

涉及到 KDE 的部分: kscreenlocker

PAM 模块借助 conv_func 机制 -> 讲解如何编写 PAM Application
编辑 KDE 的 QML 文件 -> kscreenlocker 源码， Qt 的 Signal 机制，`Q_EMIT`，观察 `conv_func` -> 修改 QML 对应部分

TODO: 将 PAM 模块的原理放在这里比较合适？此外画图图解一下

# Linux 串口编程与文件 IO

在正式进入主题之前，我们需要先了解一下什么是系统调用：

系统调用 (Syscall) 是受控的内核入口，借用这一机制，一个程序可以请求内核以自己的名义去执行某些动作，它 **在编程角度上来说和调用 C 语言的 API 并无区别**，但在底层角度，系统调用会涉及到处理器的用户态到核心态的切换、中断机制和汇编等内容，这些内容我们在这里并不是特别关心，但还是需要大概知道一下。

对于 Linux C 系统编程，这些 API **大部分都会在调用失败时返回 -1 并设置对应的 errno 值**，我们可以通过 `strerrno(errno)` 来将错误码转字符串，如果有些系统调用并不遵循该原则，我会特别说明。Linux Syscalls 可以通过 `man syscalls` 来查看，我们本次要用到的系统调用主要是 `open`, `write`, `read` 等。

## 串口设备的打开和基础配置

得益于 Linux 一切皆文件的设计，串口在 Linux 的表现形式为名为 `/dev/ttyXn` 的一个文件。而由于串口的一些特殊属性（波特率、奇偶校验等）， Linux 还开放了一些 API 用于设置串口属性，这些 API 主要在 `termios.h` 里面。 因此串口编程实际上要做的事情就是：打开串口文件 -> 配置串口属性[可选, 默认 9600, 8n1, 无流控] -> 读写串口文件 -> 关闭串口文件。

因此我们先来打开串口文件：

- `int open(const char *pathname, int flags);`

这个函数我相信不需要过多的介绍，`pathname` 就是文件路径，而 `flags` 就是打开文件的标志位。
我们需要用到的 `flag`: `O_RDWR`, `O_NOCTTY`, `O_NONBLOCK`

我们需要读写串口，因此需要使用 `O_RDWR` 打开串口文件，而由于 Linux 会将串口抽象为 tty 设备，也就意味着默认情况下，系统可能会把串口当作一个控制终端来用，因此就可能能够发送一些特殊的控制字符，比如 `^C`，由于实际我们去使用的时候并不会发送这些特殊控制字符，因此需要借助 `O_NOCTTY` 关闭这一选项。

`O_NONBLOCK` 从字面意思上来理解就是不要阻塞，即以非阻塞的形式打开文件，这个选项的作用我会放到下面讲。

然后就轮到配置串口了：

比较基础的，如设置波特率这些的代码比较简单，写起来也很无聊，因此我是直接抄的 《嵌入式 Linux 开发教程》 里面的代码，但有些配置选项则值得说道说道：

如果说上面的选项指示系统如何打开串口文件，那么下面的选项就指示我们系统要如何与串口通信，我们期望收发纯粹的二进制数据，对于输入，由于数据手册并没要求使用流控，因此我们把软件流控关掉：

```c
opt.c_iflag &= ~(IXON | IXOFF | IXANY);
```

而对于输出显示之类的，什么 `<CR><LF>`, `<LF>` 之类的都是没有必要的字符，我们并不需要，对应的也就是关掉输出处理（使用原始输出）、关掉规范模式和关掉回显等：

```c
opt.c_oflag &= ~OPOST; // 使用原始输出
// 关掉规范模式，回显，终端信号等
opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
```

## 串口配置中的 `vtime` & `vmin` 以及串口通信的设计取舍

然后我们就来到了 `struct termios->c_cc` 的配置选项了，其他的选项我们并不关心，我们只需要看 `vtime` & `vmin` 即可。为什么要看这两个选项呢？这就要说一下串口和这个指纹模组的特殊地方了。

串口文件和存储在磁盘中的普通文件不同，对于普通的文件，你打开之后要读多少、写多少，你/代码心理都有数，**但是**串口是一种通信设备，我们大部分时候可能都是开着这个设备等数据，只有那么一小段时间设备突然发送一段数据流过来，**而且**通信设备是可能断连的，这就引出了两个代码设计上的思考：

1. 我们是要等到海枯石烂还是到点就走？

- 死等也就意味着卡死在这里，而到点就走又需要定义超时时的行为

2. 如果串口断开了怎么办？

我们这里的实验代码参考 [rd_nb.c][rd_nb.c]

对于第一个权衡点，这时就要提到之前说的 `O_NONBLOCK` 标志位了：
串口在非阻塞读的情况下，不论有无数据，都会立即返回成功，所以你会看到非常抽象的情况：

```bash
19:35:34 DEBUG ./rd_nb.c:23: write 12 bytes
write:        EF 01 FF FF FF FF 01 00 03 35 00 39
19:35:34 DEBUG ./rd_nb.c:22: current errno: 0 => Success
19:35:34 ERROR ./rd_nb.c:37: read failed: Success # 读取失败但是成功，这是因为 read 直接返回 0
```

如果设置了阻塞，那么此时对串口的读操作状态就会**取决于** `vtime` 和 `vmin` 这两个变量，前者表示单次读取的最大等待时间(单位 0.1s, 等待时间范围为 0 ~ 25.5s，对应传入 0 ~ 255)，后者表示单次读取需要接收到的最少字节数， 这两个变量有点类似于 `timeout` 的性质，设置的值有如下四种情况：

1. `vtime = 0, vmin = 0`: 此时对应不阻塞，不论如何都会直接返回，一般来说都不建议这种方式
2. `vtime > 0, vmin = 0`: 此时会等待最大 vtime 的时间，只要有数据就会返回
3. `vtime = 0, vmin > 0`: 此时要求至少收集到 vmin 个字节后才返回，因此少于 vmin 个字节的行为是未定义的
4. `vtime > 0, vmin > 0`: 此时要求只要拿到 vmin 个字节，或者到了 vtime 要求的时间， read 就会成功返回

受限于篇幅原因我们只需要了解上面那些就足够了，如果想要了解更多东西，可以参考[这篇文章][vtime & vmin]

对于第二点，实际上我们有一个很好用的检测设备是否断连的方法，我们只需要借助 `tcgetattr` 即可，这个函数在尝试获取串口配置失败时会返回 -1, 并设置 errno 为一些特定的值，如果 `errno == EIO`，那么一般就是串口断连了。严谨一些的话可以用 `poll`，但是我没用，这里会在后面解释 TODO:

这里可能会有人问，为什么不直接判断 `read` 后的 `errno`，问题在于第一次 `read` 返回的读取字节数为 0, 此时代表的实际上是 `read` 认为读到 EOF 了，这是成功的标志，因此不会设置 `errno`，你就无法通过这个来判断，因此要判断设备是否被拔出就必须再操作一次串口文件，因此就需要调用 `tcgetattr`，这是比较简单的：

- 不调用 `tcgetattr` 的情况（那个 `serial_is_dev_hangup()` 函数是我的一个简易封装，就是我在讲解第二点一开始时提到的方法）:

![check errno when tcgetattr not called][check errno when tcgetattr not called]

- 调用 `tcgetattr` 的情况，其中 gdb 终端那一行表示显示调用 errno 对应的函数并解引用返回的地址后得到的返回值，可以看到返回值为 5(`EIO`):

![check errno when tcgetattr called][check errno when tcgetattr called]

## 串口多进程的数据竞争

我一开始说了，由于我自己犯贱改需求，所以本来只是用于解锁 KDE 锁屏的，并不需要考虑多进程问题，但扩展需求之后就必须要考虑多进程情形了。
比如最简单的，一个终端跑 `sudo su`， 一个终端跑 `sudo apt update && sudo apt upgrade -y`，这种情况下，如果你使用指纹模块，你要如何确保不会导致数据竞争？万一两边此时都要求验证了，然后同时对设备发送 `auto_identify`，万一有一边发送了 `cancel`，那么另一边还验证不验证…这些问题很麻烦，但都可以通过**最朴素的加锁方式**来实现。

当然，首先看下加锁与不加锁的区别，下面是不加锁的：

![fp-zw111-test-without-lock][fp-zw111-test-without-lock]

而这个是加锁的：

![fp-zw111-test-with-lock][fp-zw111-test-with-lock]

看完对比之后你就会理解为什么要加锁了，那么问题来了：怎么加锁？加什么锁？

## Linux 中的文件锁

## 总结

最后我们再来梳理一下整体的流程：

打开串口文件 -> 确认文件是 tty 设备[可选] -> 给文件加锁 -> 配置串口选项 -> 返回文件描述符

这就是 [protocol.c][protocol.c] 中 `fp_open` 函数的基本思路，那么 `fp_close` 实际上就是对称的关系：先解锁再关闭文件。

---

思考串口通信，并将这些点融入文章中

- 串口设备文件 IO 需要考虑多进程？ => 互斥锁 (lockf)
- 设备插拔断联导致的异常应当怎么处理？
- 读需要读超时？ => 设备超时帧/代码设定的超时时间
- 写？ => 一次性写入/分大小写？
- 读？ => 一次性读完/分大小读？

---

2. `ssize_t write(int fd, const void *buf, size_t count);`

3. `ssize_t read(int fd, void *buf, size_t count);`

4. `int close(int fd);`

TODO: 再简单介绍一下 `ioctl`，以及为什么用 `tcgetattr` 替代

# ZW111 模块开发

这部分不想过多赘述，基本上就是照着手册开发就行。

从需求的角度上来说我们需要开发的功能实际上只有：
- 注册指纹(auto_enroll)
- 验证指纹(auto_identify)

其余的比如 `read_sys_para` 和 `check_sensor` 之类的其实是衍生功能，比如指纹满了要删除点库存、取消本次自动验证/注册，等等，但是 `get_image` 是确实有必要的，我在开发过程中测试发现如果你不在指纹验证错误并要求重试时加入这个，并要求用户移开手指，那么指纹模组会直接开始第二次识别，也就是说可能会白白浪费很多重试次数。

代码见[这里][protocol.c]

# PAM 模块

## PAM 基本流程和简易模块开发

网上看到的简中 PAM 模块教程基本上都是只有配置文件的，但是我们要写 ZW111 的指纹识别模块的话需要串起配置文件和模块开发的流程，因此需要借助一手资料，下面已经列举出了。

---

PAM 模块整体的工作流程：

PAM 应用程序指定对应的 service --> 应用程序发起 `auth` 请求 --> libpam 在 service 内部配置找到对应 so 文件/模块 --> `dlopen` 打开 so 文件并调用对应函数 --> 处理结果返回 libpam 并进行下一步处理 --> PAM 程序收到 API 的返回码并进行对应的处理

PAM 配置文件有一定的语法规则，由于配置文件的相关资料很多，这里只对需要用到的做简要说明。

基本语法规则： `<type> <flag> <path> <args>`
比如这个例子： `auth required xxx.so`，就表示 authentication （用户身份验证） 要求使用 `xxx.so` 这个模块， `xxx.so` 需要实现一些 PAM 模块要求的函数，然后通过返回 `PAM_*` 状态码告知是验证通过还是失败。

我们会用到的只有 auth, required 和 sufficient. auth 就是鉴权用的，验证你是不是对应的用户， required 和另一个叫 requisite 的很像， required 表示调用所有需要的模块，即使有模块失败了也先不管，等整体调用完了再返回失败，而另一个则是一失败就立马返回， sufficient 表示只要验证通过就直接返回成功，并且自动忽略前面模块的 fail。由于 sufficient 有成功就直接返回的特性，我并不建议使用它，接下来我只会在测试中使用。

---

下面是具体的实验方法

首先需要先安装 libpam：

```bash
sudo apt install libpam-dev -y
```

编写 [pam_auth_test.c](./test/pam-test/pam_auth_test.c) 文件，我们这里只作为测试用，需要注意永远不要明文将密钥写入代码中，比如下面用 ltrace 去看的：

```bash
> ltrace -e strcmp ./test_pam pam-auth-test ss ss 2>&1 | grep "pam_auth_test.so"
pam_auth_test.so->strcmp("ss", "test", 4)       = -1
```

然后一眼就知道密码是什么了

然后编译，注意需要编译成共享库：

```bash
gcc -fPIC -shared -o pam_auth_test.so pam_auth_test.c
```

TODO: 需要审查 fPIC 的讲解

PIC: Position Independent Code

fPIC 标志表示需要生成位置无关的代码，因为我们要编译出来的是共享库（即动态链接库），因此自然不可以是位置有关的，如果位置有关，那么这个库就会对外部编译产物有地址位置上的要求，这违背了共享和动态链接的要求 —— 加载一份共享库，所有依赖该库的都能直接调用，不需要将其复制到可执行文件上，更详细的可以去看其他大佬的介绍或者看 CSAPP 第七章关于链接的介绍。

接下来将 so 链接到对应的位置，我这里在 `/lib/x86_64-linux-gnu/security` 中

```bash
# 测完之后记得删掉
sudo ln $(pwd)/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

我个人认为链接最好用硬链接，只要你的盘在同一个文件系统内就行，硬链接的好处在于两个文件都指向了同一 inode 且修改任何一个文件的内容，其他文件都会跟着更新，删除任意文件都不会导致已有的文件失效，除非全删光。

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

这样子通路就基本搭建完毕了，然后我们接下来自己写一个 [应用程序](./pam_auth_test.c)

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

但是我们测试还是会用更加成熟的 `pamtester`，首先安装它

```bash
> sudo apt install pamtester -y
```

然后我们来演示下这个工具的用法：

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

PAM 模块的编写教程到这里就结束了

## PAM 和 KDE 的联动

我的桌面环境是 KDE Plasma 6：

```bash
> plasmashell --version
plasmashell 6.3.6
```

KDE 本身自带一个锁屏登录模块，而刚开机时则一般由 SDDM 或者其他的 Display Manager 接管，这里不详细讨论这个问题，只要知道它是独立于 Desktop Environment 的一个应用，用于在 shell 日志滚完之后接管其显示，启动顺序是 shell -> DM -> DE， KDE 一般和 SDDM 搭配使用，在 SDDM 验证身份之后，由 KDE 接替接下来的显示工作（占用一个新的 tty），并且之后也是 KDE 来负责会话的管理（锁屏，解锁，切换用户等）

KDE 和 SDDM 各自会占用一个 tty， SDDM 在验证通过之后你再切过去 (ctrl + alt + fx, x 是多少自己试一下就知道了) 会发现是卡死的，不过这点对于我们接下来要做的事来说不重要。

首先还是老规矩，简单编写一个[模块](./test/pam-test/pam_kde_auth_test.c)并链接过去，然后测试一下

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

TODO:
![][]

接下来我会建议注释原来的 `pam_fprintd.so` 选项，改用自己自定义的 `.so` 作为指纹模块：
TODO: 这里是否可以借鉴： `auth    required        pam_succeed_if.so user != root quiet_success` 的编辑思路来实现两个文件其中一个参与？

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

然后我们执行如下步骤：

```bash
> mkdir -p ~/.local/share/plasma/shells/org.kde.plasma.desktop/ && cp -r /usr/share/plasma/shells/org.kde.plasma.desktop/ ~/.local/share/plasma/shells/

> ln -f /home/ferne/code/self-proj/fingerprint/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml ~/.local/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/MainBlock.qml
```

然后锁屏界面就会替换成你自己自定义的

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

建议是写一个文件然后在其他文件里面 `include` 这个文件，
我写了个 [pam-fp-zw111](./test/pam_test/pam-fp-zw111)，然后链接过去
接下来测试就可以 `pamtester pam-fp-zw111 $USER authenticate` 来写，更方便

那么我们现在写的 [pam_fprint_zw111](./src/pam_fprint_zw111.c) 是没有 `flock` 的，
当然我们现在是只在 kde-fingerprint 上用，但假如我们想加到 `sudo` 这个命令呢？或者任何有可能多进程的地方呢？事实上不上锁的情况对于单进程来说是没什么问题的，然而对于多进程来说，这种设计可能引发数据竞争，导致两个/多个进程读写的数据不准( [实验参考](test/fd-test) )，考虑到这一层，我们上锁是一个必然的操作。

这里并不详细介绍文件上锁，详细讲解可以参考《Linux/Unix 系统编程手册》的第 55 章，对于我们这种只想要单一时间内只让一个进程访问设备的需求，我们应当选择互斥锁 `flock(fd, LOCK_EX);`。

我设计了一个指纹模块加入锁相关代码前后的对照实验，我的操作看视频，总的来说我希望两个进程都能正常完成验证：

不带锁的，可以看到数据竞争可能导致卡死：

NOTE: 两个视频的录像都有问题，重录

![fp-zw111-test-without-lock](./images/fp-zw111-test-without-lock.webm)

---

带锁的，进程之间先到先得，符合互斥锁的现象：

![fp-zw111-test-with-lock](./images/fp-zw111-test-with-lock.webm)

不上锁时需要修改的代码

```
  if (lockf(fd, F_TLOCK, 0) == -1) {
    if (errno == EACCES || errno == EAGAIN) {
      log_error("file: %s (fd: %d) locked!", devname, fd);
    }
    // fp_close(fd);
    // return -2;
  }
```

```bash
> sudo adduser "testusr"

> pamtester pam-fp-zw111 "testusr" authenticate
17:27:46 INFO  src/pam_fprint_zw111.c:92: pam_sm_authenticate called with flags: 0x00, argc: 0
17:27:46 INFO  src/pam_fprint_zw111.c:59: now openning device: /dev/FP_ZW111...
17:27:46 INFO  src/protocol.c:255: now openning device /dev/FP_ZW111...
17:27:46 INFO  src/protocol.c:269: device /dev/FP_ZW111 opened, now configuring terminal parameters...
17:27:46 INFO  src/pam_fprint_zw111.c:78: device opened!
设备已连接
17:27:46 INFO  src/pam_fprint_zw111.c:113: please place your finger on the module
17:27:46 DEBUG src/protocol.c:124: fill pack: id=1 code=32 data_len=5 total_len=17 checksum=0240
send:         EF 01 FF FF FF FF 01 00 08 32 02 FF FF 00 05 02 40
17:27:46 DEBUG src/protocol.c:163: write 17 bytes
recv:         EF 01 FF FF FF FF 07 00 08 00 05 00 01 04 A9 00 C2
17:27:48 DEBUG src/protocol.c:244: nbr: 17, p_recv->code: 0x00
17:27:48 INFO  src/pam_fprint_zw111.c:161: identified!
pamtester: successfully authenticated
```

---

[项目开源链接]: https://github.com/aliferne/fp-zw111-unlocker
[vtime & vmin]: http://unixwiz.net/techtips/termios-vmin-vtime.html

[rd_nb.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/test/wrrd-test/rd_nb.c
[protocol.c]: https://github.com/aliferne/fp-zw111-unlocker/blob/main/src/protocol.c
[check errno when tcgetattr not called]: ./images/check-errno-when-tcgetattr-not-called.png
[check errno when tcgetattr called]: ./images/check-errno-when-tcgetattr-called.png
[fp-zw111-test-without-lock]: ./images/fp-zw111-test-without-lock.webm
[fp-zw111-test-with-lock]: ./images/fp-zw111-test-with-lock.webm

- 《Linux 系统编程》 Chapter 1, 2, 11
- 《Linux/Unix 系统编程手册》 Chapter 3, 23, 55, 附录 B —— 解析命令行选项
- 《嵌入式 Linux 开发教程》（上） Chapter 16

1. https://zhuanlan.zhihu.com/p/663757925
2. https://wiki.archlinux.org.cn/title/PAM
3. flag fPIC: https://zhuanlan.zhihu.com/p/368364069
   同时 fPIC 选项可以参考 CSAPP 的第七章
4. gettext, msg(un)fmt command: https://www.cnblogs.com/jjxhp/p/12036187.html

下面这些是一手资料，质量更好：

1. http://www.fifi.org/doc/libpam-doc/html
2. Linux man page of pam (`man pam`)
3. pamtester: https://pamtester.sourceforge.net/
