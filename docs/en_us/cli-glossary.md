# subcli Commands and Options Chinese-English Glossary

subcli is a subscription management and proxy client configuration generation tool.

It does not directly enable system proxy, nor does it replace Mihomo, sing-box, or Xray. Its primary functions are initializing a workspace, saving subscription URLs and configurations, updating subscription nodes, and exporting target client configuration files based on profiles/templates.

## First Use

```bash
subcli init
subcli doctor
subcli sub add --name my-sub --url <your subscription link>
subcli sub update
subcli export mihomo
```

`subcli init` creates and remembers the default workspace. Subsequent commands will use this workspace by default, so there is no need to specify `--workspace` every time.

## Common Commands

| English Command/Option | Chinese Meaning | Example |
| --- | --- | --- |
| `init` | Initialize and remember the default workspace | `subcli init` |
| `workspace init` | Initialize a workspace and set it as default | `subcli workspace init ./ws` |
| `workspace status` | View the current workspace | `subcli workspace status` |
| `workspace use` | Switch the default workspace | `subcli workspace use ./ws2` |
| `workspace unset` | Clear the default workspace | `subcli workspace unset` |
| `doctor` | Check if the environment and configuration are normal | `subcli doctor` |
| `sub add` | Add a subscription | `subcli sub add --name my-sub --url https://example/sub` |
| `sub update` | Update subscriptions | `subcli sub update` |
| `sub list` | List subscriptions | `subcli sub list` |
| `export mihomo` | Export Mihomo configuration | `subcli export mihomo` |
| `export sing-box` | Export sing-box configuration | `subcli export sing-box` |
| `export xray` | Export Xray configuration | `subcli export xray` |
| `--workspace DIR` | Temporarily use a specific workspace for this command | `subcli --workspace ./ws doctor` |
| `--output-dir DIR` | Specify the export directory | `subcli export mihomo --output-dir ./outputs` |
| `--json` | Output in JSON format | `subcli doctor --json` |
| `--strict-network` | Strictly report errors on network failure | `subcli sub update --strict-network` |
| `--help` / `-h` | View help | `subcli --help` |

## What is a Workspace

A workspace is the working directory used by subcli under the hood to store configurations, subscriptions, templates, resources, cache, export files, and runtime state.

Regular users typically only need to run once:

```bash
subcli init
```

Advanced users can use the following commands to switch or clear the default workspace:

```bash
subcli workspace use ./another-workspace
subcli workspace unset
```

## Common Workflows

### Add a Subscription and Export Mihomo Configuration

```bash
subcli sub add --name my-sub --url https://example/sub
subcli sub update
subcli export mihomo
```

### Temporarily Use Another Workspace

```bash
subcli --workspace ./other-workspace doctor
```

`--workspace` only affects the current command and does not change the remembered default workspace.
