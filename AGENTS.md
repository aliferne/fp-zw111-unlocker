# 项目概览

使用 ZW111 模块和 Linux 系统编程来实现类似于 Windows Hello 的指纹解锁功能
硬件连接为 ZW111 -> USB 转 TTL（CH340） -> PC

硬件由人工审查，Agent 只需要负责软件的审查工作

基本技术栈:

- termios.h （嵌入式 Linux 串口编程）
- Linux PAM 模块

串口工具：

- stty, printf/cat
- minicom
- vofa+ （该可视化工具仅供人类调用）

# 项目规约

1. **不准**擅自修改文件，除非得到允许
2. 对于疑问需要较为详细的回答，对于方案提议需要思考是否存在其他更优解
3. FP 模块的协议参考 `docs/ZW111-*.md`
4. `docs` 中的 pdf 已用 `pdftotext` 转为文本，放在 `ai/ZW111-manual-V1.5.1.txt`（用户手册）和 `ai/ZW111-spec-V1.2.txt`（规格书），Agent **可直接阅读这两份文本**来查协议/指令；原始 pdf 仍不建议直接读，需要时可重新用 `pdftotext <pdf> <txt>` 转换
5. 脚本，代码等除非特殊声明，否则写入 `ai/` 文件夹下
6. 作者正在学习 Linux 系统开发，尽量倾向于给出建议**而不是**直接丢解决方案
