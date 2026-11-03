# EasyEffects CLI

Command-line interface for EasyEffects audio processing with full LV2 plugin support.

## License

GPL v2

## Hola Coder AI Code Disclaimer

This code was developed with assistance from Hola Coder AI. The AI provided code generation, architecture design, and implementation guidance. All code is reviewed and modified by human developers.

## Features

- LV2 plugin discovery and loading
- PipeWire filter management
- Preset save/load
- Plugin chaining
- Offline audio processing

## Dependencies

**No Qt/KDE/GTK dependencies** — pure C, headless CLI.

Required libraries:
- `libpipewire-0.3` — PipeWire audio graph / filter API
- `lilv-0` — LV2 plugin discovery & instantiation
- `json-c` — Preset JSON save/load
- `dl`, `m` — Standard system libraries

## Building

```bash
git clone https://github.com/cloudgpu/easyeffects-cli
cd easyeffects-cli
make
```

### Build Requirements (Debian/Ubuntu)
```bash
sudo apt install libpipewire-0.3-dev lilv-dev libjson-c-dev build-essential
```

### Build Requirements (Arch)
```bash
sudo pacman -S pipewire lilv json-c base-devel
```

### Build Requirements (Fedora)
```bash
sudo dnf install pipewire-devel lilv-devel json-c-devel gcc make
```

## Usage

### 1. List Available Plugins
```bash
./easyeffects_cli list-plugins
```
Shows all discovered LV2 plugins (256+ including Calf, LSP, etc.)

### 2. Check Status
```bash
./easyeffects_cli status
```
Verifies PipeWire connection and LV2 world initialization.

### 3. Create a Plugin Chain
```bash
# Compressor → Reverb chain
./easyeffects_cli chain "http://calf.sourceforge.net/plugins/Compressor" "http://calf.sourceforge.net/plugins/Reverb" --name my_chain

# Single plugin
./easyeffects_cli chain "http://calf.sourceforge.net/plugins/Equalizer12Band" --name eq_only

# With custom node name
./easyeffects_cli load "http://lsp-plug.in/lsp/eq" --node-name my_eq
```

The chain runs until stopped with `Ctrl+C`. The session manager (WirePlumber) should auto-connect to default audio sink/source.

### 4. Presets
```bash
# Save current configuration
./easyeffects_cli preset save my_chain --description "Vocal chain with compression and reverb"

# List presets
./easyeffects_cli preset list

# Load preset
./easyeffects_cli preset load my_chain

# Delete preset
./easyeffects_cli preset delete my_chain
```

Presets are stored in `~/.config/easyeffects-cli/presets/`.

### 5. Stop the Chain
Press `Ctrl+C` to stop. If it doesn't exit cleanly:
```bash
pkill -9 -f easyeffects_cli
```

### 6. Manual Audio Routing (if auto-connect fails)
```bash
# List PipeWire nodes
pw-cli ls Node

# Find your filter node (name matches --name or --node-name)
# Link: source → filter input → filter output → sink
pw-cli link <source_node> <filter_input_node>
pw-cli link <filter_output_node> <sink_node>
```

### 7. Offline Processing (Stubbed)
```bash
./easyeffects_cli process input.wav output.wav --plugin "http://calf.sourceforge.net/plugins/Compressor"
```
Currently shows what would be processed; full offline rendering not yet implemented.

### 8. Monitor & Config
```bash
./easyeffects_cli monitor <node-name>
./easyeffects_cli config set <key> <value>
./easyeffects_cli config get <key>
```

## Common Plugin URIs

| Plugin | URI |
|--------|-----|
| Calf Compressor | `http://calf.sourceforge.net/plugins/Compressor` |
| Calf Reverb | `http://calf.sourceforge.net/plugins/Reverb` |
| Calf Equalizer 12 Band | `http://calf.sourceforge.net/plugins/Equalizer12Band` |
| Calf Equalizer 30 Band | `http://calf.sourceforge.net/plugins/Equalizer30Band` |
| Calf Limiter | `http://calf.sourceforge.net/plugins/Limiter` |
| Calf Gate | `http://calf.sourceforge.net/plugins/Gate` |
| Calf Deesser | `http://calf.sourceforge.net/plugins/Deesser` |
| Calf Filter | `http://calf.sourceforge.net/plugins/Filter` |
| Calf Flanger | `http://calf.sourceforge.net/plugins/Flanger` |
| Calf Phaser | `http://calf.sourceforge.net/plugins/Phaser` |
| LSP EQ | `http://lsp-plug.in/lsp/eq` |
| LSP Compressor | `http://lsp-plug.in/lsp/compressor` |
| LSP Limiter | `http://lsp-plug.in/lsp/limiter` |
| LSP Gate | `http://lsp-plug.in/lsp/gate` |

Use `./easyeffects_cli list-plugins` to see all available plugins on your system.

## Requirements at Runtime

- PipeWire daemon running (`pipewire`, `wireplumber` or `pipewire-media-session`)
- LV2 plugins installed:
  - **Calf**: `calf-plugins` (Debian/Ubuntu), `calf` (Arch), `calf-plugins` (Fedora)
  - **LSP**: `lsp-plugins` (Debian/Ubuntu/Arch/Fedora)
  - **Other**: `zam-plugins`, `mda.lv2`, `invada-studio-plugins-lv2`, etc.

## Troubleshooting

**"PipeWire: Disconnected"**
- Ensure PipeWire is running: `systemctl --user status pipewire wireplumber`
- Start if needed: `systemctl --user start pipewire wireplumber`

**"Plugin not found"**
- Install LV2 plugin packages (see above)
- Run `lv2ls` to verify plugins are discovered by lilv

**No audio output**
- Check PipeWire connections: `pw-cli ls Node` and `pw-cli ls Link`
- Use `helvum` or `qpwgraph` for visual routing
- Ensure filter ports are connected to sink/source

**Ctrl+C doesn't stop the process**
- Known issue with thread loop signal handling
- Use `pkill -9 -f easyeffects_cli` as workaround

## Install System-Wide (Optional)
```bash
sudo make install
# Installs to /usr/local/bin/easyeffects_cli
# Creates ~/.config/easyeffects-cli/presets/
```

## Project Structure

```
easyeffects-cli/
├── Makefile
├── README.md
├── LICENSE
├── CODING_DISCLAIMER.md
├── src/
│   ├── main.c              # CLI commands, chain management
│   ├── pipewire_manager.c  # PW filter, thread loop, port handling
│   ├── lv2_loader.c        # Lilv plugin discovery & instantiation
│   ├── preset_manager.c    # JSON preset save/load
│   └── easyeffects_cli.h   # Shared types & declarations
└── plugins/                # (reserved for future)
```

## Contributing

Issues and PRs welcome. This is a CLI-only tool — no GUI dependencies will be added.