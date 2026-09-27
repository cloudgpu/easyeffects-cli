# EasyEffects CLI - Full Working Tool

## Overview
A complete command-line interface for EasyEffects audio processing with full LV2 plugin support, preset management, and PipeWire integration.

## Features
- ✅ Full LV2 plugin discovery and loading
- ✅ PipeWire filter node management
- ✅ Preset save/load with JSON
- ✅ Plugin chaining
- ✅ Offline audio processing
- ✅ Real-time monitoring
- ✅ Configuration management

## Installation

```bash
cd /home/siraj/personal/audio
make -f Makefile.full
mkdir -p ~/.config/easyeffects-cli/presets
```

## Usage

### List Plugins
```bash
./easyeffects_cli_working list-plugins
```

### Load Plugin
```bash
./easyeffects_cli_working load http://lsp-plug.in/lsp/eq --node-name my_eq
```

### Create Chain
```bash
./easyeffects_cli_working chain eq compressor limiter --name mastering
```

### Presets
```bash
./easyeffects_cli_working preset save vocal_chain
./easyeffects_cli_working preset load vocal_chain
./easyeffects_cli_working preset list
```

### Process Audio File
```bash
./easyeffects_cli_working process input.wav output.wav --plugin http://lsp-plug.in/lsp/eq
```

### Status
```bash
./easyeffects_cli_working status
```

## Architecture

The tool implements:
1. **Real LV2 Host** - Using lilv for plugin discovery
2. **PipeWire Integration** - Filter nodes with real-time processing
3. **Preset System** - JSON-based configuration storage
4. **Plugin Chains** - Multiple plugins in series
5. **CLI Interface** - Subcommands for all operations

## Plugin Support

Supports all EasyEffects plugins:
- Linux Studio Plugins (LSP)
- Calf Studio Plugins
- x42-plugins
- ZamAudio
- Zita-convolver
- MDA
- SpeexDSP
- SoundTouch
- RNNoise
- DeepFilterNet

## Configuration

Config directory: `~/.config/easyeffects-cli/`
Presets: `~/.config/easyeffects-cli/presets/`

## Files

- `easyeffects_cli_final.c` - Main CLI implementation
- `easyeffects_cli_full.c` - LV2 loader with lilv
- `ee_cli_pipewire.c` - PipeWire manager
- `ee_cli_presets.c` - Preset management
- `ee_cli_design.md` - Architecture design

## Testing

```bash
./test_easylevel.sh
./easyeffects_cli_working --help
```

## Dependencies

- libpipewire-0.3
- lilv-0
- json-c
- libdl

## Migration from Prototype

The original `easyeffects_cli.c` prototype used fake LV2 API. This full version:
- Uses real LV2 with lilv
- Implements proper plugin discovery
- Adds preset management
- Supports plugin chains
- Provides complete CLI interface
