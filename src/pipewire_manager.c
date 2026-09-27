#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/properties.h>
#include <pipewire/keys.h>
#include <pipewire/thread-loop.h>
#include <pipewire/context.h>
#include <pipewire/core.h>

typedef struct {
    struct pw_filter *filter;
    struct pw_filter_port *ports[8];
    int port_count;
    struct pw_loop *loop;
    void *userdata;
} pw_filter_t;

static void on_process(void *userdata, struct spa_io_position *position) {
    pw_filter_t *f = userdata;
    printf("Processing audio: %u samples\n", position->clock.duration);
}

static void on_state_changed(void *userdata, enum pw_filter_state old, enum pw_filter_state state, const char *error) {
    printf("Filter state changed: %d -> %d\n", old, state);
}

static const struct pw_filter_events filter_events = {
    .version = 0,
    .state_changed = on_state_changed,
    .process = on_process,
};

pw_filter_t* pw_filter_create(const char *name, struct pw_loop *loop) {
    pw_filter_t *f = calloc(1, sizeof(pw_filter_t));
    if (!f) return NULL;
    
    f->loop = loop;
    
    struct pw_properties *props = pw_properties_new(NULL, NULL);
    pw_properties_set(props, PW_KEY_APP_NAME, "easyeffects-cli");
    pw_properties_set(props, PW_KEY_NODE_NAME, name);
    pw_properties_set(props, PW_KEY_MEDIA_TYPE, "Audio");
    pw_properties_set(props, PW_KEY_MEDIA_CATEGORY, "DSP");
    pw_properties_set(props, PW_KEY_MEDIA_ROLE, "Effect");
    pw_properties_set(props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(props, PW_KEY_AUDIO_RATE, "48000");
    
    f->filter = pw_filter_new_simple(loop, name, props, &filter_events, f);
    if (!f->filter) {
        free(f);
        return NULL;
    }
    
    // Create input ports
    struct pw_properties *port_props = pw_properties_new(NULL, NULL);
    pw_properties_set(port_props, PW_KEY_FORMAT_DSP, "32 bit float");
    
    // Left input
    pw_properties_set(port_props, "audio.position", "FL");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "input_FL");
    f->ports[0] = pw_filter_add_port(f->filter, PW_DIRECTION_INPUT, 
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0, 
                                      port_props, NULL, 0);
    
    // Right input
    pw_properties_set(port_props, "audio.position", "FR");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "input_FR");
    f->ports[1] = pw_filter_add_port(f->filter, PW_DIRECTION_INPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                      port_props, NULL, 0);
    
    // Left output
    pw_properties_set(port_props, "audio.position", "FL");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "output_FL");
    f->ports[2] = pw_filter_add_port(f->filter, PW_DIRECTION_OUTPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                      port_props, NULL, 0);
    
    // Right output
    pw_properties_set(port_props, "audio.position", "FR");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "output_FR");
    f->ports[3] = pw_filter_add_port(f->filter, PW_DIRECTION_OUTPUT,
                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                      port_props, NULL, 0);
    
    pw_properties_free(port_props);
    
    if (pw_filter_connect(f->filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0) < 0) {
        pw_filter_destroy(f->filter);
        free(f);
        return NULL;
    }
    
    f->port_count = 4;
    return f;
}

void pw_filter_destroy_filter(pw_filter_t *f) {
    if (f) {
        if (f->filter) {
            pw_filter_destroy(f->filter);
        }
        free(f);
    }
}

int pw_link_ports(pw_filter_t *f, const char *source_node, const char *source_port,
                  const char *sink_node, const char *sink_port) {
    // Implementation for port linking via PipeWire API
    printf("Linking %s:%s -> %s:%s\n", source_node, source_port, sink_node, sink_port);
    return 0;
}

int main() {
    pw_init(NULL, NULL);
    
    struct pw_thread_loop *loop = pw_thread_loop_new("ee-cli", NULL);
    pw_thread_loop_start(loop);
    
    struct pw_loop *pl = pw_thread_loop_get_loop(loop);
    struct pw_context *context = pw_context_new(pl, NULL, 0);
    pw_context_create_core(context, NULL, 0);
    
    pw_filter_t *filter = pw_filter_create("ee_cli_test", pl);
    if (filter) {
        printf("Filter created successfully\n");
        sleep(2);
        pw_filter_destroy_filter(filter);
    }
    
    pw_context_destroy(context);
    pw_thread_loop_stop(loop);
    pw_thread_loop_destroy(loop);
    pw_deinit();
    
    return 0;
}
