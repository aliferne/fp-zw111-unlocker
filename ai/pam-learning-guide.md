# PAM 学习与指纹模块集成笔记

面向本项目的 PAM 入门，目标是写一个 `pam_fingerprint.so`，让登录/锁屏/sudo 能通过
ZW111 指纹认证。本文只讲思路与要点，不展开 PAM 全部 API。

## 1. PAM 是什么

PAM = Pluggable Authentication Modules（可插拔认证模块）。

- 应用（login、sudo、gdm、锁屏）不自己写认证逻辑，而是在认证时向 PAM 请求。
- PAM 读取 `/etc/pam.d/<服务名>`，按配置依次调用若干 `pam_*.so` 模块。
- 每个模块对结果投一票（success / fail），最终由 control flag 决定放行还是拒绝。

调用链大致是：

```
应用 → pam_start(service, user, ...) → pam_authenticate()
     → 按 /etc/pam.d/<service> 逐条执行模块的 pam_sm_authenticate()
     → 汇总结果返回给应用
```

## 2. 配置文件语法

`/etc/pam.d/<service>` 每行格式：

```
<type>  <control>  <module>  [args...]
```

- **type**：`auth`（认证）、`account`、`password`、`session`。指纹只用 `auth`。
- **control**：
  - `required`：必须成功，失败也要等其它模块跑完才报错（防探测）。
  - `sufficient`：**成功即直接放行**；失败则忽略、继续下一个模块。
  - `optional`、`requisite`（失败立即返回）等。
- **module**：`pam_fingerprint.so`。

推荐把指纹放成"加分项"，保留密码回退（避免传感器坏了把人锁在门外）：

```
# /etc/pam.d/zw111-test
auth  sufficient  pam_fingerprint.so
auth  required    pam_unix.so
```

## 3. 需要实现的接口

主要是这两个（`account/session/password` 本项目用不到）：

```c
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv);
PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags,
                              int argc, const char **argv); // 可空实现
```

`pam_sm_authenticate` 里要做的事：

1. `pam_get_user(pamh, &user, NULL)` 拿到本次要认证的用户名。
2. 打开串口、初始化（复用现有 serial/protocol 层）。
3. `fp_auto_identify()` 让用户按手指，拿到匹配到的指纹 ID（见第 4 节，需改 API 回传 ID）。
4. 查 `ID → 用户名` 绑定表，要求查出的用户名 == 本次请求的 `user`。
5. 返回：
   - 匹配成功 → `PAM_SUCCESS`
   - 不匹配 / 超时 → `PAM_AUTH_ERR`
   - 串口打不开 / 模块无响应 → `PAM_AUTHINFO_UNAVAIL`（让上层回退密码）
6. 日志用 `pam_syslog(pamh, LOG_*|PAM_...)`，**不要**直接 printf（会污染终端/对话）。

## 4. ID ↔ 用户名 映射（本项目最关键的设计）

**不要**让"任意已录指纹解锁任意账户"，也**不要**直接拿指纹 ID 当 UID。

推荐做法：

- 维护一张绑定表，如 `/etc/zw111/users.conf`：

  ```
  alice 1
  bob   2
  ```

  或每用户单独存（放在各自 home 下，配合权限）。

- 认证时必须是 `identify` 得到的 ID 反查到用户名，且**等于** PAM 请求的 username。

- 录入（enroll）与绑定建议单独做一个管理工具/CLI，而不是放在 PAM 里。

## 5. 实现注意点（踩坑清单）

- **阻塞与超时**：识别要等人放手指，`pam_authenticate` 会阻塞。务必设合理超时，
  并支持取消（本模块已有 `fp_cancel()` / 指令 0x30）。
- **并发访问串口**：多个登录/解锁可能同时触发 PAM。串口是独占资源，访问前加
  文件锁（`flock`），否则收发会交错。
- **权限**：PAM 服务通常以 root 运行，打开 `/dev/ttyUSB0`（属 `dialout`）没问题。
  但要注意管理工具以普通用户运行时的权限。
- **别把指纹设成唯一因素**：用 `sufficient` + 密码 `required` 回退。
- **不要读用户口头输入**：`pam_get_user` 拿的是 PAM 会话里的 username；除非服务允许，
  不要自己 `pam_prompt` 问用户名。
- **线程安全**：某些服务多线程调用，注意 PAM 给的是独立 `pamh`，但共享的串口句柄要保护。
- **错误信息**：不要向用户泄露"哪个用户存在指纹"这类信息。

## 6. 部署与测试

编译（PAM 模块是共享库）：

```sh
gcc -shared -fPIC -o pam_fingerprint.so pam_fingerprint.c \
    protocol.c serial.c -Iinc -lpam -lpthread
```

安装位置（发行版相关，常见两种）：

```
/lib/x86_64-linux-gnu/security/pam_fingerprint.so
/usr/lib/security/pam_fingerprint.so
```

**安全测试流程（务必照做）**：

1. 先建**独立**测试服务 `/etc/pam.d/zw111-test`，不要动 `/etc/pam.d/sudo`。
2. 用 `pamtester zw111-test <user> authenticate` 验证（或自己写个小程序调
   `pam_authenticate`）。
3. **保留一个已登录的 root shell** 兜底，防止配置错误把自己锁在外面。
4. 确认无误后再接入 `sudo` / `login` / `gdm` 等。

## 7. 参考

- `man pam`、`man pam_authenticate`、`man pam.conf`、`man pam_sm_authenticate`
- Linux-PAM 官方文档：<https://www.linux-pam.org/Linux-PAM-html/>
- 现成参考实现：`pam_fprintd.so`（配合 fprintd/libfprint）
