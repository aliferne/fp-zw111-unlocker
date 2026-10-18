# PAM 模块的学习

网上看到的简中 PAM 模块教程基本上都是只有配置文件的，但是我们要写 ZW111 的指纹识别模块的话需要串起配置文件和模块开发的流程，因此需要借助一手资料，下面已经列举出了。

---

PAM 模块整体的工作流程：

PAM 应用程序指定对应的 service --> 应用程序发起 `auth` 请求 --> libpam 在 service 内部配置找到对应 so 文件/模块 --> 请求通过 `dlopen` 等方式派发到对应文件/模块进行处理（验证） --> 处理结果返回 libpam 并进行下一步处理 --> PAM 程序收到 API 的返回码并进行对应的处理

service 文件不带 .service 后缀还是挺神奇的，需要注意该文件是 DSL，有一定的语法规则，由于配置文件的相关资料很多，这里只对需要用到的做简要说明。

---

下面是具体的实验方法

首先需要先安装 libpam：

```bash
sudo apt install libpam-dev -y
```

编写 [pam_auth_test.c](./pam_auth_test.c) 文件，然后编译，注意需要编译成共享库：

```bash
gcc -fPIC -shared -o pam_auth_test.so pam_auth_test.c
```

接下来将 so 链接到对应的位置，我这里在 `/lib/x86_64-linux-gnu/security` 中

```bash
sudo ln -s $(pwd)/pam_auth_test.so /lib/x86_64-linux-gnu/security
```

然后编写服务文件

```bash
tee > pam-auth-test << EOF
auth required pam_auth_test.so
account required pam_auth_test.so
EOF
```

最后链接配置文件

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
> ./test_pam pam-auth-test test test
[pam_testauth] pam_sm_authenticate called
[pam_testauth] got user='test' pass='test'
[pam_testauth] MATCH -> PAM_SUCCESS
pam_authenticate -> 0 (Success)
```

PAM 模块学习草稿基本写这么简单就行，后面写博客再扩充

# 参考资料

1. https://zhuanlan.zhihu.com/p/663757925
2. https://wiki.archlinux.org.cn/title/PAM

下面这些是一手资料，质量更好：

1. http://www.fifi.org/doc/libpam-doc/html
2. Linux man page of pam (`man pam`)
