#ifndef EASYEFFECTS_CLI_H
#define EASYEFFECTS_CLI_H

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <lilv/lilv.h>
#include <json-c/json.h>
#include <stdbool.h>

#define MAX_PLUGINS 256
#define MAX_PARAMS 512
#define MAX_CHAIN_PLUGINS 16

/* Plugin information */
typedef struct {
    char uri[512];
    char name[256];
    char path[512];
    LilvPlugin *lilv_plugin;
    LilvUIs *uis;
} plugin_info_t;

/* LV2 port/parameter info */
typedef struct {
    uint32_t index;
    char symbol[128];
    char name[128];
    char type[64];
    float min, max, default_val;
    bool is_input, is_output, is_control;
} port_info_t;

/* Plugin instance with LV2 state */
typedef struct {
    LilvPlugin *plugin;
    LilvInstance *instance;
    LilvState *state;
    port_info_t ports[MAX_PARAMS];
    int port_count;
    float *port_buffers[MAX_PARAMS];
    void *lv2_handle;
} plugin_instance_t;

/* Chain of plugins */
typedef struct {
    char name[256];
    plugin_instance_t *plugins[MAX_CHAIN_PLUGINS];
    int plugin_count;
    struct pw_filter *filter;
    struct pw_thread_loop *loop;
} plugin_chain_t;

/* Preset structure */
typedef struct {
    char name[256];
    char description[512];
    char chain[1024];
    struct {
        char plugin_uri[512];
        char plugin_name[256];
        char param_name[128];
        double value;
        char type[32];
    } params[256];
    int param_count;
} preset_t;

/* Global state */
extern LilvWorld *g_world;
extern LilvPlugins *g_all_plugins;
extern plugin_info_t g_plugin_registry[MAX_PLUGINS];
extern int g_registry_count;

/* lv2_loader.c */
void discover_plugins(const char *path);
void list_plugins(void);
int find_plugin_by_uri(const char *uri);
plugin_info_t* get_plugin_info(int index);
int get_plugin_count(void);
plugin_instance_t* plugin_instance_new(const char *uri, double sample_rate);
void plugin_instance_free(plugin_instance_t *inst);
int plugin_instance_get_port_count(plugin_instance_t *inst);
const port_info_t* plugin_instance_get_port(plugin_instance_t *inst, int index);
int plugin_instance_set_port_value(plugin_instance_t *inst, const char *symbol, float value);
float plugin_instance_get_port_value(plugin_instance_t *inst, const char *symbol);
void plugin_instance_run(plugin_instance_t *inst, uint32_t n_samples);

/* pipewire_manager.c */
typedef struct pw_manager pw_manager_t;
pw_manager_t* pw_manager_new(void);
void pw_manager_free(pw_manager_t *mgr);
int pw_manager_create_filter(pw_manager_t *mgr, const char *name, plugin_chain_t *chain);
int pw_manager_connect_ports(pw_manager_t *mgr, const char *src_node, const char *src_port,
                             const char *dst_node, const char *dst_port);
int pw_manager_connect_to_system(pw_manager_t *mgr, const char *input_node, const char *output_node);
int pw_manager_activate(pw_manager_t *mgr);
void pw_manager_deactivate(pw_manager_t *mgr);
struct pw_loop* pw_manager_get_loop(pw_manager_t *mgr);
struct pw_thread_loop* pw_manager_get_thread_loop(pw_manager_t *mgr);

/* preset_manager.c */
int preset_save(const char *name, preset_t *preset);
int preset_load(const char *name, preset_t *preset);
void preset_list(void);
int preset_delete(const char *name);

/* main.c commands */
int cmd_list_plugins(int argc, char *argv[]);
int cmd_load(int argc, char *argv[]);
int cmd_chain(int argc, char *argv[]);
int cmd_preset(int argc, char *argv[]);
int cmd_process(int argc, char *argv[]);
int cmd_monitor(int argc, char *argv[]);
int cmd_status(int argc, char *argv[]);
int cmd_config(int argc, char *argv[]);

#endif