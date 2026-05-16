# subcli 命令和选项中文对照表

subcli 是一个订阅管理和代理客户端配置生成工具。

它不会直接开启系统代理，也不会替代 Mihomo、sing-box 或 Xray。它的主要作用是初始化 workspace、保存订阅 URL 和配置、更新订阅节点，并根据 profile/template 导出目标客户端配置文件。

## 第一次使用

```bash
subcli init
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

`subcli init` 会创建并记住默认 workspace。之后的命令会默认使用这个 workspace，不需要每次都写 `--workspace`。

## 常用命令

| 英文命令/选项 | 中文含义 | 示例 |
| --- | --- | --- |
| `init` | 初始化并记住默认 workspace | `subcli init` |
| `workspace init` | 初始化 workspace 并设为默认 | `subcli workspace init ./ws` |
| `workspace status` | 查看当前 workspace | `subcli workspace status` |
| `workspace use` | 切换默认 workspace | `subcli workspace use ./ws2` |
| `workspace unset` | 清除默认 workspace | `subcli workspace unset` |
| `doctor` | 检查环境和配置是否正常 | `subcli doctor` |
| `sub add` | 添加订阅 | `subcli sub add --name my-sub --url https://example/sub` |
| `sub update` | 更新订阅 | `subcli sub update` |
| `sub list` | 查看订阅列表 | `subcli sub list` |
| `export mihomo` | 导出 Mihomo 配置 | `subcli export mihomo` |
| `export sing-box` | 导出 sing-box 配置 | `subcli export sing-box` |
| `export xray` | 导出 Xray 配置 | `subcli export xray` |
| `--workspace DIR` | 本次命令临时使用某个 workspace | `subcli --workspace ./ws doctor` |
| `--output-dir DIR` | 指定导出目录 | `subcli export mihomo --output-dir ./outputs` |
| `--json` | 用 JSON 格式输出 | `subcli doctor --json` |
| `--strict-network` | 网络失败时严格报错 | `subcli sub update --strict-network` |
| `--help` / `-h` | 查看帮助 | `subcli --help` |

## workspace 是什么

workspace 是 subcli 在幕后使用的工作目录，用来存放配置、订阅、模板、资源、缓存、导出文件和运行状态。

普通用户通常只需要运行一次：

```bash
subcli init
```

高级用户可以用下面的命令切换或清除默认 workspace：

```bash
subcli workspace use ./another-workspace
subcli workspace unset
```

## 常见流程

### 添加订阅并导出 Mihomo 配置

```bash
subcli sub add --name my-sub --url https://example/sub
subcli sub update
subcli export mihomo
```

### 临时使用另一个 workspace

```bash
subcli --workspace ./other-workspace doctor
```

`--workspace` 只影响当前这一次命令，不会改变已经记住的默认 workspace。
