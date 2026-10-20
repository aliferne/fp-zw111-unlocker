# PAM 模块的学习

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
sudo ln -s $(pwd)/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

我这里链接的话用的是软链接，我个人比较推荐大家用硬链接，这样目标文件夹就会有一个副本，大家都指向相同的 inode, 修改任意一处另一处都会同步被修改，当然最重要的是删了一个，另一个不受影响，我这里随便写着玩的就无所谓了

然后编写服务文件

```bash
tee pam-auth-test << EOF
auth required pam_auth_test.so
account required pam_auth_test.so
EOF
```

最后链接这个服务文件

```bash
sudo ln -s $(pwd)/pam-auth-test /etc/pam.d
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
> gcc -fPIC -shared -o pam_kde_auth_test.so pam_kde_auth_test.c && sudo ln -s /$(pwd)/pam_kde_auth_test.so /lib/x86_64-linux-gnu/security
```

注意， KDE 的 PAM 服务文件在 `/usr/lib/pam.d` 里面，这里有个优先级的问题，一般是 `/etc/pam.d` 里面的优先调用，我们接下来修改 `kde-fingerprint` 即可，其他的别动：

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
> gcc -fPIC -shared -o pam_fprint_zw111.so pam_fprint_zw111.c && sudo ln -s $(pwd)/pam_fprint_zw111.so /lib/x86_64-linux-gnu/security
```

然后测试

# 参考资料

1. https://zhuanlan.zhihu.com/p/663757925
2. https://wiki.archlinux.org.cn/title/PAM
3. [flag fPIC]: https://zhuanlan.zhihu.com/p/368364069
   同时 fPIC 选项可以参考 CSAPP 的第七章

下面这些是一手资料，质量更好：

1. http://www.fifi.org/doc/libpam-doc/html
2. Linux man page of pam (`man pam`)
3. [pamtester]: https://pamtester.sourceforge.net/
