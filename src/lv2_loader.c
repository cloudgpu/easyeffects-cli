#include "easyeffects_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lilv/lilv.h>
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

LilvWorld *g_world = NULL;
LilvPlugins *g_all_plugins = NULL;
plugin_info_t g_plugin_registry[MAX_PLUGINS];
int g_registry_count = 0;

static LilvNode *g_input_port_class = NULL;
static LilvNode *g_output_port_class = NULL;
static LilvNode *g_control_port_class = NULL;

static void init_port_classes(void) {
    if (!g_world) return;
    if (!g_input_port_class) {
        g_input_port_class = lilv_new_uri(g_world, LV2_CORE__InputPort);
        g_output_port_class = lilv_new_uri(g_world, LV2_CORE__OutputPort);
        g_control_port_class = lilv_new_uri(g_world, LV2_CORE__ControlPort);
    }
}

static void load_plugin_ports(LilvPlugin *plugin, plugin_instance_t *inst) {
    init_port_classes();
    
    uint32_t num_ports = lilv_plugin_get_num_ports(plugin);
    
    inst->port_count = 0;
    
    for (uint32_t i = 0; i < num_ports && inst->port_count < MAX_PARAMS; i++) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
        if (!port) continue;
        
        port_info_t *pinfo = &inst->ports[inst->port_count];
        pinfo->index = i;
        
        const LilvNode *port_symbol = lilv_port_get_symbol(plugin, port);
        const LilvNode *port_name = lilv_port_get_name(plugin, port);
        
        if (port_symbol) strncpy(pinfo->symbol, lilv_node_as_string(port_symbol), 127);
        else snprintf(pinfo->symbol, 127, "port_%u", i);
        
        if (port_name) strncpy(pinfo->name, lilv_node_as_string(port_name), 127);
        
        pinfo->is_input = lilv_port_is_a(plugin, port, g_input_port_class);
        pinfo->is_output = lilv_port_is_a(plugin, port, g_output_port_class);
        pinfo->is_control = lilv_port_is_a(plugin, port, g_control_port_class);
        
        LilvScalePoints *scale_points = lilv_port_get_scale_points(plugin, port);
        if (scale_points) {
            LilvIter *sp_iter;
            for (sp_iter = lilv_scale_points_begin(scale_points); 
                 !lilv_scale_points_is_end(scale_points, sp_iter); 
                 sp_iter = lilv_scale_points_next(scale_points, sp_iter)) {
                const LilvScalePoint *sp = lilv_scale_points_get(scale_points, sp_iter);
                const LilvNode *value_node = lilv_scale_point_get_value(sp);
                if (value_node) {
                    float val = lilv_node_as_float(value_node);
                    if (pinfo->min == 0.0f && pinfo->max == 0.0f) {
                        pinfo->min = val;
                        pinfo->max = val;
                    } else {
                        if (val < pinfo->min) pinfo->min = val;
                        if (val > pinfo->max) pinfo->max = val;
                    }
                    if (pinfo->default_val == 0.0f) pinfo->default_val = val;
                }
            }
            lilv_scale_points_free(scale_points);
        }
        
        LilvNode *def = NULL, *min = NULL, *max = NULL;
        lilv_port_get_range(plugin, port, &def, &min, &max);
        if (def) pinfo->default_val = lilv_node_as_float(def);
        if (min) pinfo->min = lilv_node_as_float(min);
        if (max) pinfo->max = lilv_node_as_float(max);
        if (def) lilv_node_free(def);
        if (min) lilv_node_free(min);
        if (max) lilv_node_free(max);
        
        inst->port_count++;
    }
}

void discover_plugins(const char *path) {
    (void)path;
    if (!g_world) {
        g_world = lilv_world_new();
        lilv_world_load_all(g_world);
        g_all_plugins = lilv_world_get_all_plugins(g_world);
    }
    
    g_registry_count = 0;
    
    LilvIter *i;
    for (i = lilv_plugins_begin(g_all_plugins); !lilv_plugins_is_end(g_all_plugins, i); i = lilv_plugins_next(g_all_plugins, i)) {
        LilvPlugin *plugin = lilv_plugins_get(g_all_plugins, i);
        
        if (g_registry_count >= MAX_PLUGINS) break;
        
        const LilvNode *uri_node = lilv_plugin_get_uri(plugin);
        const LilvNode *name_node = lilv_plugin_get_name(plugin);
        
        const char *uri = uri_node ? lilv_node_as_string(uri_node) : "";
        const char *name = name_node ? lilv_node_as_string(name_node) : "";
        
        strncpy(g_plugin_registry[g_registry_count].uri, uri, 511);
        strncpy(g_plugin_registry[g_registry_count].name, name, 255);
        g_plugin_registry[g_registry_count].lilv_plugin = plugin;
        g_plugin_registry[g_registry_count].uis = lilv_plugin_get_uis(plugin);
        g_registry_count++;
    }
}

void list_plugins(void) {
    printf("Available LV2 Plugins (%d total):\n", g_registry_count);
    printf("========================================\n");
    for (int i = 0; i < g_registry_count; i++) {
        printf("%3d. %s\n", i+1, g_plugin_registry[i].name);
        printf("     URI: %s\n", g_plugin_registry[i].uri);
    }
}

int find_plugin_by_uri(const char *uri) {
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_plugin_registry[i].uri, uri) == 0) {
            return i;
        }
    }
    return -1;
}

plugin_info_t* get_plugin_info(int index) {
    if (index >= 0 && index < g_registry_count) {
        return &g_plugin_registry[index];
    }
    return NULL;
}

int get_plugin_count(void) {
    return g_registry_count;
}

plugin_instance_t* plugin_instance_new(const char *uri, double sample_rate) {
    int idx = find_plugin_by_uri(uri);
    if (idx < 0) {
        fprintf(stderr, "Plugin not found: %s\n", uri);
        return NULL;
    }
    
    LilvPlugin *plugin = g_plugin_registry[idx].lilv_plugin;
    if (!plugin) {
        fprintf(stderr, "Invalid plugin\n");
        return NULL;
    }
    
    plugin_instance_t *inst = calloc(1, sizeof(plugin_instance_t));
    if (!inst) return NULL;
    
    inst->plugin = plugin;
    
    inst->instance = lilv_plugin_instantiate(plugin, sample_rate, NULL);
    
    if (!inst->instance) {
        fprintf(stderr, "Failed to instantiate plugin: %s\n", uri);
        free(inst);
        return NULL;
    }
    
    load_plugin_ports(plugin, inst);
    
    for (int i = 0; i < inst->port_count; i++) {
        inst->port_buffers[i] = calloc(4096, sizeof(float));
    }
    
    lilv_instance_activate(inst->instance);
    
    return inst;
}

void plugin_instance_free(plugin_instance_t *inst) {
    if (!inst) return;
    
    if (inst->instance) {
        lilv_instance_deactivate(inst->instance);
        lilv_instance_free(inst->instance);
    }
    
    for (int i = 0; i < inst->port_count; i++) {
        free(inst->port_buffers[i]);
    }
    
    free(inst);
}

int plugin_instance_get_port_count(plugin_instance_t *inst) {
    return inst ? inst->port_count : 0;
}

const port_info_t* plugin_instance_get_port(plugin_instance_t *inst, int index) {
    if (inst && index >= 0 && index < inst->port_count) {
        return &inst->ports[index];
    }
    return NULL;
}

int plugin_instance_set_port_value(plugin_instance_t *inst, const char *symbol, float value) {
    if (!inst) return -1;
    
    for (int i = 0; i < inst->port_count; i++) {
        if (strcmp(inst->ports[i].symbol, symbol) == 0) {
            if (inst->ports[i].is_control) {
                inst->port_buffers[i][0] = value;
                return 0;
            }
        }
    }
    return -1;
}

float plugin_instance_get_port_value(plugin_instance_t *inst, const char *symbol) {
    if (!inst) return 0.0f;
    
    for (int i = 0; i < inst->port_count; i++) {
        if (strcmp(inst->ports[i].symbol, symbol) == 0) {
            if (inst->ports[i].is_control) {
                return inst->port_buffers[i][0];
            }
        }
    }
    return 0.0f;
}

void plugin_instance_run(plugin_instance_t *inst, uint32_t n_samples) {
    if (!inst || !inst->instance) return;
    
    for (int i = 0; i < inst->port_count; i++) {
        lilv_instance_connect_port(inst->instance, inst->ports[i].index, inst->port_buffers[i]);
    }
    
    lilv_instance_run(inst->instance, n_samples);
}