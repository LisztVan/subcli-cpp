# subcli 命令和选项中文对照表

subcli 是一个订阅管理和代理客户端配置生成工具。

它不会直接开启系统代理，也不会替代 Mihomo、sing-box 或 Xray。它的主要作用是初始化 workspace、保存订阅 URL 和配置、更新订阅节点，并根据 profile/template 导出目标客户端配置文件。

## 第一次使用

```bash
subcli config init --portable
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

`subcli config init` 会在可执行文件同级目录创建 `config.yaml`。之后的命令会默认使用这个配置。

## 常用命令

| 英文命令/选项 | 中文含义 | 示例 |
| --- | --- | --- |
| `config init` | 初始化配置 | `subcli config init --portable` |
| `doctor` | 检查环境和配置是否正常 | `subcli doctor` |
| `sub add` | 添加订阅 | `subcli sub add --name my-sub --url https://example/sub` |
| `sub update` | 更新订阅 | `subcli sub update` |
| `sub list` | 查看订阅列表 | `subcli sub list` |
| `export mihomo` | 导出 Mihomo 配置 | `subcli export mihomo` |
| `export sing-box` | 导出 sing-box 配置 | `subcli export sing-box` |
| `export xray` | 导出 Xray 配置 | `subcli export xray` |
| `--config PATH` | 本次命令临时使用某个配置 | `subcli --config /path/to/config.yaml doctor` |
| `--output-dir DIR` | 指定导出目录 | `subcli export mihomo --output-dir ./outputs` |
| `--json` | 用 JSON 格式输出 | `subcli doctor --json` |
| `--strict-network` | 网络失败时严格报错 | `subcli sub update --strict-network` |
| `--help` / `-h` | 查看帮助 | `subcli --help` |
| `purge` | 清理/彻底清理 | `subcli purge --all --yes` |

## config.yaml 是什么

`config.yaml` 是 subcli 的配置文件，用来定义路径、网络限制、模板路径、核心路径、资源 URL 和分组规则。

普通用户通常只需要运行一次：

```bash
subcli config init --portable
```

这会生成默认的 `config.yaml`，所有相对路径都相对于应用程序目录（即可执行文件所在目录）解析。

高级用户可以用下面的命令指定不同的配置路径：

```bash
subcli --config /path/to/config.yaml doctor
```

或通过环境变量：

```bash
export SUBCLI_CONFIG=/path/to/config.yaml
subcli doctor
```

## 常见流程

### 添加订阅并导出 Mihomo 配置

```bash
subcli sub add --name my-sub --url https://example/sub
subcli sub update
subcli export mihomo
```

### 临时使用另一个配置

```bash
subcli --config /path/to/other-config.yaml doctor
```

`--config` 只影响当前这一次命令，不会改变已存在的配置。
