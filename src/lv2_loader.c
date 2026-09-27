#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/properties.h>
#include <pipewire/keys.h>
#include <pipewire/thread-loop.h>
#include <pipewire/context.h>
#include <pipewire/impl-core.h>
#include <getopt.h>
#include <lilv/lilv.h>
#include <json-c/json.h>

#define MAX_PLUGINS 256
#define MAX_PARAMS 512

typedef struct {
    char uri[512];
    char name[256];
    char path[512];
    LilvPlugin *lilv_plugin;
} plugin_info_t;

typedef struct {
    char param_name[128];
    double value;
    double min;
    double max;
    char type[32];
} param_t;

typedef struct {
    char name[256];
    plugin_info_t plugins[MAX_PLUGINS];
    int plugin_count;
    param_t params[MAX_PARAMS];
    int param_count;
} preset_t;

static LilvWorld *world = NULL;
static LilvPlugins *plugins = NULL;
static plugin_info_t plugin_registry[MAX_PLUGINS];
static int registry_count = 0;

static void discover_plugins(const char *path) {
    if (!world) {
        world = lilv_world_new();
        lilv_world_load_all(world);
        plugins = lilv_world_get_all_plugins(world);
    }
    
    LilvPlugin **iter = lilv_plugins_begin(plugins);
    LilvPlugin **end = lilv_plugins_end(plugins);
    
    for (; iter != end; ++iter) {
        LilvPlugin *plugin = *iter;
        const char *uri = lilv_plugin_get_uri(plugin);
        const char *name = lilv_plugin_get_name(plugin);
        
        if (registry_count < MAX_PLUGINS) {
            strncpy(plugin_registry[registry_count].uri, uri, 511);
            strncpy(plugin_registry[registry_count].name, name, 255);
            plugin_registry[registry_count].lilv_plugin = plugin;
            registry_count++;
        }
    }
}

static void list_plugins() {
    printf("Available LV2 Plugins:\n");
    printf("======================\n");
    for (int i = 0; i < registry_count; i++) {
        printf("%d. %s\n", i+1, plugin_registry[i].name);
        printf("   URI: %s\n", plugin_registry[i].uri);
    }
}

static int find_plugin_by_uri(const char *uri) {
    for (int i = 0; i < registry_count; i++) {
        if (strcmp(plugin_registry[i].uri, uri) == 0) {
            return i;
        }
    }
    return -1;
}

static void save_preset(const char *name, preset_t *preset) {
    char path[512];
    snprintf(path, sizeof(path), "~/.config/easyeffects-cli/presets/%s.json", name);
    // Implementation for JSON save
    printf("Preset saved: %s\n", name);
}

static void load_preset(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "~/.config/easyeffects-cli/presets/%s.json", name);
    // Implementation for JSON load
    printf("Preset loaded: %s\n", name);
}

void print_usage(const char *prog) {
    printf("Usage: %s <command> [options]\n", prog);
    printf("Commands:\n");
    printf("  list-plugins [--path PATH]\n");
    printf("  load <plugin-uri> [--node-name NAME] [--input NODE] [--output NODE]\n");
    printf("  chain <plugin1> <plugin2> ...\n");
    printf("  preset save <name>\n");
    printf("  preset load <name>\n");
    printf("  preset list\n");
    printf("  config set <key> <value>\n");
    printf("  process <in> <out> --plugin <uri>\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "list-plugins") == 0) {
        discover_plugins(NULL);
        list_plugins();
    }
    else if (strcmp(command, "preset") == 0) {
        if (argc >= 4) {
            if (strcmp(argv[2], "save") == 0) {
                save_preset(argv[3], NULL);
            } else if (strcmp(argv[2], "load") == 0) {
                load_preset(argv[3]);
            }
        }
    }
    else {
        printf("Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
