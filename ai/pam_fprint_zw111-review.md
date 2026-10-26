# pam_fprint_zw111.c 代码审查

审查对象：`src/pam_fprint_zw111.c`（含当前工作区对 `src/protocol.c` 的未提交修改）
运行环境：KDE Plasma 6.3.5（`libkscreenlocker6 6.3.5`），PAM 跑在 `kscreenlocker_greet` 的 `PamWorker` QThread 中。

上一轮提到的几点已经修复：

- `pam_open_fp()` 不再无限重开，失败会返回 `-1`；
- 区分了 `FP_ERR_FINGER`（设备确认码 `0x09`）；
- `FP_ERR_IO` 会关闭旧 fd 并尝试重连；
- 使用了 `nanosleep` 做延迟，不再忙等。

下面按优先级列出仍存在的问题。

---

## 必须先修（会导致死循环 / 卡死）

### 1. 未处理的 status 会无限循环

位置：`src/pam_fprint_zw111.c:113-138`

循环只处理了三种状态：

```c
if (status == FP_ERR_TIMEOUT) { ... continue; }
else if (status == FP_ERR_IO) { ... }
if (status == FP_ERR_FINGER) { ... }
```

其余状态（`FP_FAIL`、`FP_ERR_PROTO`）没有任何分支：既不 `continue`、也不计数、也不退出，于是每 500ms 重来一次，永远出不去。

而 `fp_auto_identify()` 会把以下确认码都归为 `FP_FAIL`（`src/protocol.c:421-435`）：

- `0x17` 残留指纹 / 手指没有移动
- `0x24` 指纹库为空
- `0x23` 模板不存在
- `0x0B` ID 非法
- `0x01` 其他失败

其中 **`0x17` 正是“没及时移开手指产生的第二个失败帧”**。现在的效果不是浪费一次机会，而是：

```
0x17 -> FP_FAIL -> 无分支 -> delay 500ms -> 0x17 -> ...
```

并且永远不消耗 retry。这比原来的问题更严重。

建议改成 `switch`，并且必须有 `default`：

- `0x17`：不计入 retry，等待用户移开手指后重试；最好配合 `0x01 PS_GetImage` 轮询到 `0x02`（无手指）再继续；
- `0x24 / 0x23 / 0x0B`：重试没有意义，直接 `pam_error` + 返回 `PAM_AUTHINFO_UNAVAIL`；
- `0x09`：计入 retry；
- `0x01 / 0x07 / FP_ERR_PROTO`：计入 retry 或直接失败，不能静默循环。

### 2. `FP_ERR_TIMEOUT` 不计数，且没有总时限

位置：`src/pam_fprint_zw111.c:116-118`

`timeout` 分支 `continue` 且不增加 retry。如果用户一直不放手指，每约 11s 一轮，永远循环。`MAX_TIMEOUT` 目前只用于 `pam_open_fp()`，认证主循环没有任何 deadline。

在 KDE 6.3.5 上尤其危险：PAM 跑在 `PamWorker` QThread 里，`PamAuthenticator` 析构会无限 `m_thread.wait()`（`git show v6.3.5:greeter/pamauthenticator.cpp`）。指纹线程不返回，用密码解锁后 greeter 可能退不出去。

建议给整个 `pam_sm_authenticate` 加一个单调时钟 deadline（例如 30~60s，可配置），超时返回 `PAM_AUTH_ERR`；`FP_ERR_TIMEOUT` 可以继续重试，但必须受总 deadline 约束。

### 4. 两种 `FP_ERR_TIMEOUT` 无法区分

位置：`src/protocol.c:421-435`

- 模组返回 `0x26`：设备正常，只是没检测到手指；
- `fp_transact` 的 `poll` 超时：设备根本没回包，可能还在处理上一条命令。

现在两者都返回 `FP_ERR_TIMEOUT`，PAM 里都 `continue`。但第二种情况下不应该直接在同一条 fd 上重发 `0x32`，否则可能读到上一条命令的残留响应，造成响应错位（极端情况下旧的成功帧被当成本次成功）。

建议：

- 增加 `FP_ERR_DEV_TIMEOUT`（`0x26`）和 `FP_ERR_NO_RESPONSE`（传输超时）两个状态；
- 传输超时后先 `fp_cancel()` + `tcflush()`，必要时重开设备，再继续。

---

## 高优先级

### 5. 等待时间仍然不可配置

位置：`:114,136`

上一轮的需求是“等待时间可配置”，但现在是两个硬编码的 `delay_ms(500)`，`argv` 完全没用（编译器也在警告 `unused parameter 'argv'`）。

建议解析 PAM 参数：

```
auth required pam_fprint_zw111.so retry_delay_ms=800 max_retry=5 total_timeout_ms=30000 device=/dev/FP_ZW111
```

同时注意 `FP_ERR_FINGER` 分支会先在循环顶部等 500ms，再在分支里等 500ms，实际是 1s。确认这是不是想要的效果；如果只要一个 500ms，应把顶部 delay 移到分支里统一处理。

### 8. 退出路径没有 `fp_cancel()`

`fp_auto_identify` 正常返回时命令已经结束，但传输超时/异常路径不一定。建议在所有退出路径 best-effort 调用 `fp_cancel(fd)`（在 `fp_close` 之前），并 `tcflush`，避免把模组留在自动识别状态，影响下一次认证。

### 9. 并发仍然没有锁

注释 `:87-88` 已经提到。锁屏和 `sudo` 可能同时打开 `/dev/FP_ZW111`，两条命令的帧会交错。

建议在 `fp_open` 里加 `flock(fd, LOCK_EX | LOCK_NB)`，拿不到锁返回 `PAM_AUTHINFO_UNAVAIL`。

### 10. 用户绑定仍然缺失

没有 `pam_get_user()`，`fp_auto_identify(..., id=0xFFFF, ...)` 搜索全部指纹，任何已录入手指都能认证任何账号。只接锁屏时风险较低，但作为通用模块是认证绕过。

至少加注释/限制；更好的做法是启用 `return_status` 拿到匹配 ID，再和 `pam_get_user()` 的用户名做映射。

---

## 小问题 / 清理

- `delay_ms()`（`:37-48`）里 `(ms % 1000) * 1E6` 用了 double，建议 `1000000L`。`nanosleep` 的 EINTR 处理是对的；如果以后想响应“取消/设备拔出”，换成 `poll()` 或 `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` 更合适。
- `is_time_reached()` 用 `double` 和 `1E-3` 容差有点绕；直接用 `struct timespec` 比较或用 `int64_t` 毫秒更清晰。
- `FP_ERR_FINGER = 2` 是正数，和其他负值错误码风格不一致；`fp_strstatus()`（`src/protocol.c:537`）也没处理它，会落到 `"Failed"`。
- 未使用的 `RECV_BUF_SIZE`（`:17`）、`<math.h>`、`<fcntl.h>`、`<stdint.h>` 可以删掉。
- `pam_sm_setcred` / `pam_sm_acct_mgmt` 的未使用参数可以用 `(void)` 消除警告；返回 `PAM_IGNORE` 本身没问题。
- `err_io` 的日志写死 “can not open device”，但中途设备掉线也会走到这里，文案可以更准确。
- `pam_error` 的返回值没检查；conversation 失败时至少要回退到日志。
- `log_info` 仍然写 stderr，建议 PAM 模块统一用 `pam_syslog(pamh, LOG_*)`；另外 `log.c` 的 `localtime()` 不是线程安全的。

---

## 建议的修复顺序

1. 主循环加 `switch` + `default`，杜绝未处理状态导致的死循环（最关键）。
2. 加总 deadline，并让 `FP_ERR_TIMEOUT` 受它约束。
3. 修 retry 差一，区分 `0x17`。
4. 区分“设备 `0x26` 超时”和“传输超时”，超时路径补 `fp_cancel`/flush。
5. 把 `delay_ms`、`MAX_RETRY`、open timeout 做成 PAM 参数。
6. 再处理 flock、`pam_get_user`、`pam_syslog` 这些。

其中 1~4 是当前版本会实际导致挂起/错位的问题，建议先处理。
