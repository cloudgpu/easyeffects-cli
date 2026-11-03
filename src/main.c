#include "easyeffects_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>

static volatile bool g_running = true;
static struct pw_thread_loop *g_thread_loop = NULL;

static void signal_handler(int sig) {
    (void)sig;
    printf("DEBUG: signal_handler called, g_running=%d, g_thread_loop=%p\n", g_running, (void*)g_thread_loop);
    g_running = false;
    if (g_thread_loop) {
        printf("DEBUG: Locking thread loop to signal\n");
        pw_thread_loop_lock(g_thread_loop);
        printf("DEBUG: Signaling thread loop\n");
        pw_thread_loop_signal(g_thread_loop, false);
        printf("DEBUG: Unlocking thread loop\n");
        pw_thread_loop_unlock(g_thread_loop);
    }
}

void print_help(const char *prog) {
    printf("EasyEffects CLI - Full LV2 Plugin Support\n");
    printf("==========================================\n\n");
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  list-plugins [--type lv2|ladspa|vst] [--path PATH]\n");
    printf("       List available audio plugins\n\n");
    printf("  load <plugin-uri> [--node-name NAME] [--input NODE] [--output NODE]\n");
    printf("       Load plugin into PipeWire filter chain\n\n");
    printf("  chain <plugin1> [plugin2 ...] --name CHAIN_NAME\n");
    printf("       Create multi-plugin chain\n\n");
    printf("  preset save <name> [--description DESC]\n");
    printf("       Save current configuration as preset\n\n");
    printf("  preset load <name>\n");
    printf("       Load preset configuration\n\n");
    printf("  preset list\n");
    printf("       List available presets\n\n");
    printf("  preset delete <name>\n");
    printf("       Delete a preset\n\n");
    printf("  config set <key> <value>\n");
    printf("       Set configuration option\n\n");
    printf("  config get <key>\n");
    printf("       Get configuration option\n\n");
    printf("  process <input.wav> <output.wav> --plugin <uri> [--params PARAMS]\n");
    printf("       Process audio file offline\n\n");
    printf("  monitor <node-name>\n");
    printf("       Monitor plugin node status\n\n");
    printf("  status\n");
    printf("       Show CLI status and PipeWire connection\n\n");
    printf("Examples:\n");
    printf("  %s list-plugins\n", prog);
    printf("  %s load http://lsp-plug.in/lsp/eq --node-name my_eq\n", prog);
    printf("  %s chain http://lsp-plug.in/lsp/eq http://lsp-plug.in/lsp/compressor --name mastering\n", prog);
    printf("  %s preset save vocal_chain\n", prog);
    printf("  %s process input.wav output.wav --plugin http://lsp-plug.in/lsp/eq\n", prog);
}

int cmd_list_plugins(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    discover_plugins(NULL);
    list_plugins();
    return 0;
}

int cmd_load(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Error: plugin URI required\n");
        printf("Usage: %s load <plugin-uri> [--node-name NAME]\n", argv[0]);
        return 1;
    }
    
    const char *plugin_uri = argv[2];
    const char *node_name = "ee_cli_plugin";
    
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--node-name") == 0 && i + 1 < argc) {
            node_name = argv[++i];
        }
    }
    
    printf("DEBUG: cmd_load started\n");
    printf("Loading plugin: %s\n", plugin_uri);
    printf("Node name: %s\n", node_name);
    
    discover_plugins(NULL);
    int idx = find_plugin_by_uri(plugin_uri);
    if (idx < 0) {
        printf("Plugin not found: %s\n", plugin_uri);
        return 1;
    }
    
    plugin_info_t *info = get_plugin_info(idx);
    printf("Found: %s\n", info->name);
    
    printf("DEBUG: Creating pw_manager\n");
    pw_manager_t *mgr = pw_manager_new();
    if (!mgr) {
        printf("Failed to create PipeWire manager\n");
        return 1;
    }
    
    printf("DEBUG: pw_manager created\n");
    
    plugin_chain_t *chain = calloc(1, sizeof(plugin_chain_t));
    if (!chain) {
        printf("Failed to allocate chain\n");
        pw_manager_free(mgr);
        return 1;
    }
    strncpy(chain->name, node_name, 255);
    
    printf("DEBUG: Creating plugin instance\n");
    plugin_instance_t *inst = plugin_instance_new(plugin_uri, 48000);
    if (!inst) {
        printf("Failed to instantiate plugin\n");
        free(chain);
        pw_manager_free(mgr);
        return 1;
    }
    
    printf("DEBUG: Plugin instance created\n");
    chain->plugins[0] = inst;
    chain->plugin_count = 1;
    
    printf("DEBUG: Creating filter\n");
    int create_result = pw_manager_create_filter(mgr, node_name, chain);
    printf("DEBUG: pw_manager_create_filter returned %d\n", create_result);
    if (create_result < 0) {
        printf("Failed to create filter\n");
        plugin_instance_free(inst);
        free(chain);
        pw_manager_free(mgr);
        return 1;
    }
    
    printf("DEBUG: Filter created, activating\n");
    printf("DEBUG: About to call pw_manager_activate\n");
    int activate_result = pw_manager_activate(mgr);
    printf("DEBUG: pw_manager_activate returned %d\n", activate_result);
    if (activate_result < 0) {
        printf("Failed to activate filter\n");
        plugin_instance_free(inst);
        pw_manager_free(mgr);
        return 1;
    }
    
    printf("DEBUG: Filter activated\n");
    printf("Plugin loaded and active. Press Ctrl+C to stop.\n");
    
    // Try to connect to system audio
    pw_manager_connect_to_system(mgr, "alsa_input.pci-0000_00_1f.3.analog-stereo", "alsa_output.pci-0000_00_1f.3.analog-stereo");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("DEBUG: Entering main loop\n");
    // Run the thread loop to process events - use pw_thread_loop_wait instead of pw_loop_iterate
    struct pw_thread_loop *tloop = pw_manager_get_thread_loop(mgr);
    g_thread_loop = tloop;
    int loop_count = 0;
    while (g_running) {
        if (loop_count++ % 100 == 0) {
            printf("DEBUG: Main loop iteration %d\n", loop_count);
        }
        printf("DEBUG: About to call pw_thread_loop_timed_wait\n");
        int res = pw_thread_loop_timed_wait(tloop, 1);  // 1 second timeout
        printf("DEBUG: pw_thread_loop_timed_wait returned: %d\n", res);
    }
    g_thread_loop = NULL;
    
    printf("\nStopping...\n");
    pw_manager_deactivate(mgr);
    plugin_instance_free(inst);
    pw_manager_free(mgr);
    free(chain);
    
    return 0;
}

int cmd_chain(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Error: need at least one plugin URI and --name\n");
        printf("Usage: %s chain <plugin1> [plugin2 ...] --name CHAIN_NAME\n", argv[0]);
        return 1;
    }
    
    const char *chain_name = NULL;
    const char *plugin_uris[MAX_CHAIN_PLUGINS];
    int plugin_count = 0;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            chain_name = argv[++i];
        } else if (plugin_count < MAX_CHAIN_PLUGINS) {
            plugin_uris[plugin_count++] = argv[i];
        }
    }
    
    if (!chain_name) {
        printf("Error: --name required\n");
        return 1;
    }
    
    if (plugin_count == 0) {
        printf("Error: at least one plugin required\n");
        return 1;
    }
    
    printf("Creating chain: %s\n", chain_name);
    for (int i = 0; i < plugin_count; i++) {
        printf("  Plugin %d: %s\n", i+1, plugin_uris[i]);
    }
    
    discover_plugins(NULL);
    
    pw_manager_t *mgr = pw_manager_new();
    if (!mgr) {
        printf("Failed to create PipeWire manager\n");
        return 1;
    }
    
    plugin_chain_t chain = {0};
    strncpy(chain.name, chain_name, 255);
    
    for (int i = 0; i < plugin_count; i++) {
        plugin_instance_t *inst = plugin_instance_new(plugin_uris[i], 48000);
        if (!inst) {
            printf("Failed to instantiate plugin: %s\n", plugin_uris[i]);
            for (int j = 0; j < i; j++) {
                plugin_instance_free(chain.plugins[j]);
            }
            pw_manager_free(mgr);
            return 1;
        }
        chain.plugins[i] = inst;
    }
    chain.plugin_count = plugin_count;
    
    if (pw_manager_create_filter(mgr, chain_name, &chain) < 0) {
        printf("Failed to create filter\n");
        for (int i = 0; i < plugin_count; i++) {
            plugin_instance_free(chain.plugins[i]);
        }
        pw_manager_free(mgr);
        return 1;
    }
    
    if (pw_manager_activate(mgr) < 0) {
        printf("Failed to activate filter\n");
        for (int i = 0; i < plugin_count; i++) {
            plugin_instance_free(chain.plugins[i]);
        }
        pw_manager_free(mgr);
        return 1;
    }
    
    printf("Chain active. Press Ctrl+C to stop.\n");
    
    // Try to connect to system audio
    pw_manager_connect_to_system(mgr, "alsa_input.pci-0000_00_1f.3.analog-stereo", "alsa_output.pci-0000_00_1f.3.analog-stereo");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Run the thread loop to process events - use pw_thread_loop_wait instead of pw_loop_iterate
    struct pw_thread_loop *tloop = pw_manager_get_thread_loop(mgr);
    g_thread_loop = tloop;
    while (g_running) {
        printf("DEBUG: About to call pw_thread_loop_timed_wait (chain)\n");
        int res = pw_thread_loop_timed_wait(tloop, 1);  // 1 second timeout
        printf("DEBUG: pw_thread_loop_timed_wait returned (chain): %d\n", res);
    }
    g_thread_loop = NULL;
    
    printf("\nStopping...\n");
    pw_manager_deactivate(mgr);
    for (int i = 0; i < plugin_count; i++) {
        plugin_instance_free(chain.plugins[i]);
    }
    pw_manager_free(mgr);
    
    return 0;
}

int cmd_preset(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s preset <save|load|list|delete> [name]\n", argv[0]);
        return 1;
    }
    
    const char *subcmd = argv[2];
    
    if (strcmp(subcmd, "list") == 0) {
        preset_list();
        return 0;
    }
    
    if (argc < 4) {
        printf("Error: preset name required\n");
        return 1;
    }
    
    const char *name = argv[3];
    
    if (strcmp(subcmd, "save") == 0) {
        preset_t preset = {0};
        strncpy(preset.name, name, 255);
        
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--description") == 0 && i + 1 < argc) {
                strncpy(preset.description, argv[++i], 511);
            }
        }
        
        return preset_save(name, &preset);
    }
    
    if (strcmp(subcmd, "load") == 0) {
        preset_t preset = {0};
        return preset_load(name, &preset);
    }
    
    if (strcmp(subcmd, "delete") == 0) {
        return preset_delete(name);
    }
    
    printf("Unknown preset command: %s\n", subcmd);
    return 1;
}

int cmd_process(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: %s process <input.wav> <output.wav> --plugin <uri> [--params key=value,...]\n", argv[0]);
        return 1;
    }
    
    const char *input_file = argv[2];
    const char *output_file = argv[3];
    const char *plugin_uri = NULL;
    
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin_uri = argv[++i];
        }
    }
    
    if (!plugin_uri) {
        printf("Error: --plugin URI required\n");
        return 1;
    }
    
    printf("Processing: %s -> %s\n", input_file, output_file);
    printf("Plugin: %s\n", plugin_uri);
    
    discover_plugins(NULL);
    plugin_instance_t *inst = plugin_instance_new(plugin_uri, 48000);
    if (!inst) {
        printf("Failed to instantiate plugin\n");
        return 1;
    }
    
    printf("Offline processing not fully implemented yet\n");
    printf("Would process %s through %s and write to %s\n", input_file, plugin_uri, output_file);
    
    plugin_instance_free(inst);
    return 0;
}

int cmd_monitor(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s monitor <node-name>\n", argv[0]);
        return 1;
    }
    
    printf("Monitoring node: %s\n", argv[2]);
    printf("Monitor not fully implemented yet\n");
    return 0;
}

int cmd_status(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    printf("EasyEffects CLI Status\n");
    printf("======================\n");
    
    pw_manager_t *mgr = pw_manager_new();
    if (mgr) {
        printf("PipeWire: Connected\n");
        pw_manager_free(mgr);
    } else {
        printf("PipeWire: Disconnected\n");
    }
    
    discover_plugins(NULL);
    printf("LV2 World: Initialized\n");
    printf("Plugins Available: %d\n", get_plugin_count());
    printf("Active Chains: 0\n");
    
    return 0;
}

int cmd_config(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s config <set|get> <key> [value]\n", argv[0]);
        return 1;
    }
    
    const char *subcmd = argv[2];
    const char *key = argv[3];
    
    if (strcmp(subcmd, "set") == 0) {
        if (argc < 5) {
            printf("Error: value required for set\n");
            return 1;
        }
        printf("Config set: %s = %s\n", key, argv[4]);
        return 0;
    }
    
    if (strcmp(subcmd, "get") == 0) {
        printf("Config get: %s = (not implemented)\n", key);
        return 0;
    }
    
    printf("Unknown config command: %s\n", subcmd);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help(argv[0]);
        return 0;
    }
    
    if (strcmp(cmd, "list-plugins") == 0) {
        return cmd_list_plugins(argc, argv);
    }
    
    if (strcmp(cmd, "load") == 0) {
        return cmd_load(argc, argv);
    }
    
    if (strcmp(cmd, "chain") == 0) {
        return cmd_chain(argc, argv);
    }
    
    if (strcmp(cmd, "preset") == 0) {
        return cmd_preset(argc, argv);
    }
    
    if (strcmp(cmd, "process") == 0) {
        return cmd_process(argc, argv);
    }
    
    if (strcmp(cmd, "monitor") == 0) {
        return cmd_monitor(argc, argv);
    }
    
    if (strcmp(cmd, "status") == 0) {
        return cmd_status(argc, argv);
    }
    
    if (strcmp(cmd, "config") == 0) {
        return cmd_config(argc, argv);
    }
    
    printf("Unknown command: %s\n", cmd);
    print_help(argv[0]);
    return 1;
}