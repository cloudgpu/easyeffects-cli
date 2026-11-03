#include "easyeffects_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <pipewire/properties.h>
#include <pipewire/keys.h>
#include <pipewire/thread-loop.h>
#include <pipewire/context.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/parser.h>
#include <spa/pod/builder.h>
#include <stdatomic.h>

struct pw_manager {
    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_filter *filter;
    plugin_chain_t *chain;
    bool active;
    struct spa_hook core_listener;
};

struct port_data {
    void *port_data;
    float *buffer;
    uint32_t buffer_size;
};

struct filter_data {
    pw_manager_t *mgr;
    plugin_chain_t *chain;
    struct spa_hook listener;
    struct port_data in_ports[2];
    struct port_data out_ports[2];
};

static void on_process(void *userdata, struct spa_io_position *position) {
    struct filter_data *data = userdata;
    plugin_chain_t *chain = data->chain;
    
    static int process_count = 0;
    if (process_count++ % 48000 == 0) {
        printf("DEBUG: process callback called, n_samples=%lu\n", position->clock.duration);
    }
    
    if (!chain || chain->plugin_count == 0) {
        return;
    }
    
    uint32_t n_samples = position->clock.duration;
    
    // Dequeue input buffers from filter ports
    for (int ch = 0; ch < 2; ch++) {
        if (data->in_ports[ch].port_data) {
            struct pw_buffer *buf = pw_filter_dequeue_buffer(data->in_ports[ch].port_data);
            if (buf && buf->buffer && buf->buffer->datas[0].data) {
                if (n_samples > data->in_ports[ch].buffer_size) {
                    data->in_ports[ch].buffer = realloc(data->in_ports[ch].buffer, n_samples * sizeof(float));
                    data->in_ports[ch].buffer_size = n_samples;
                }
                memcpy(data->in_ports[ch].buffer, buf->buffer->datas[0].data, n_samples * sizeof(float));
                pw_filter_queue_buffer(data->in_ports[ch].port_data, buf);
            } else {
                if (n_samples > data->in_ports[ch].buffer_size) {
                    data->in_ports[ch].buffer = realloc(data->in_ports[ch].buffer, n_samples * sizeof(float));
                    data->in_ports[ch].buffer_size = n_samples;
                }
                memset(data->in_ports[ch].buffer, 0, n_samples * sizeof(float));
            }
        }
    }
    
    // Process each plugin in the chain in series
    // First plugin gets input from filter, subsequent plugins get input from previous plugin
    float *chain_input[2] = { data->in_ports[0].buffer, data->in_ports[1].buffer };
    float *chain_output[2] = { data->out_ports[0].buffer, data->out_ports[1].buffer };
    
    for (int p = 0; p < chain->plugin_count; p++) {
        plugin_instance_t *inst = chain->plugins[p];
        if (!inst) continue;
        
        // Map filter input to plugin input ports
        for (int i = 0; i < inst->port_count; i++) {
            port_info_t *port = &inst->ports[i];
            if (port->is_input && !port->is_control) {
                // Find left/right input ports
                if (strcmp(port->symbol, "in_left") == 0 || 
                    strcmp(port->symbol, "in_1") == 0 ||
                    strcmp(port->symbol, "in_l") == 0 ||
                    strcmp(port->symbol, "input_left") == 0 ||
                    strcmp(port->symbol, "audio_in_1") == 0) {
                    memcpy(inst->port_buffers[i], chain_input[0], n_samples * sizeof(float));
                } else if (strcmp(port->symbol, "in_right") == 0 || 
                           strcmp(port->symbol, "in_2") == 0 ||
                           strcmp(port->symbol, "in_r") == 0 ||
                           strcmp(port->symbol, "input_right") == 0 ||
                           strcmp(port->symbol, "audio_in_2") == 0) {
                    memcpy(inst->port_buffers[i], chain_input[1], n_samples * sizeof(float));
                }
            }
        }
        
        // Run the plugin
        plugin_instance_run(inst, n_samples);
        
        // Map plugin output ports to chain output (for next plugin or filter output)
        for (int i = 0; i < inst->port_count; i++) {
            port_info_t *port = &inst->ports[i];
            if (port->is_output && !port->is_control) {
                if (strcmp(port->symbol, "out_left") == 0 || 
                    strcmp(port->symbol, "out_1") == 0 ||
                    strcmp(port->symbol, "out_l") == 0 ||
                    strcmp(port->symbol, "output_left") == 0 ||
                    strcmp(port->symbol, "audio_out_1") == 0) {
                    memcpy(chain_output[0], inst->port_buffers[i], n_samples * sizeof(float));
                } else if (strcmp(port->symbol, "out_right") == 0 || 
                           strcmp(port->symbol, "out_2") == 0 ||
                           strcmp(port->symbol, "out_r") == 0 ||
                           strcmp(port->symbol, "output_right") == 0 ||
                           strcmp(port->symbol, "audio_out_2") == 0) {
                    memcpy(chain_output[1], inst->port_buffers[i], n_samples * sizeof(float));
                }
            }
        }
        
        // For next plugin in chain, use this plugin's output as input
        chain_input[0] = chain_output[0];
        chain_input[1] = chain_output[1];
    }
    
    // Queue output buffers to filter ports
    for (int ch = 0; ch < 2; ch++) {
        if (data->out_ports[ch].port_data) {
            void *dsp_buf = pw_filter_get_dsp_buffer(data->out_ports[ch].port_data, n_samples);
            if (dsp_buf) {
                memcpy(dsp_buf, data->out_ports[ch].buffer, n_samples * sizeof(float));
            }
        }
    }
}

static void on_state_changed(void *userdata, enum pw_filter_state old, enum pw_filter_state state, const char *error) {
    (void)userdata;
    printf("Filter state: %d -> %d", old, state);
    if (error) printf(" (%s)", error);
    printf("\n");
}

static const struct pw_filter_events filter_events = {
    .version = PW_VERSION_FILTER_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

struct create_filter_data {
    pw_manager_t *mgr;
    const char *name;
    plugin_chain_t *chain;
    int result;
    atomic_bool done;
};

static int do_create_filter(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) {
    (void)loop; (void)async; (void)seq; (void)data; (void)size;
    struct create_filter_data *cdata = (struct create_filter_data *)user_data;
    printf("DEBUG: do_create_filter called with user_data=%p\n", user_data);
    pw_manager_t *mgr = cdata->mgr;
    plugin_chain_t *chain = cdata->chain;
    const char *name = cdata->name;
    
    printf("DEBUG: do_create_filter called, thread loop lock held\n");
    
    struct filter_data *fdata = calloc(1, sizeof(struct filter_data));
    if (!fdata) {
        cdata->result = -1;
        atomic_store(&cdata->done, true);
        return 0;
    }
    
    fdata->mgr = mgr;
    fdata->chain = chain;
    
    struct pw_properties *props = pw_properties_new(NULL, NULL);
    pw_properties_set(props, PW_KEY_APP_NAME, "easyeffects-cli");
    pw_properties_set(props, PW_KEY_NODE_NAME, name);
    pw_properties_set(props, PW_KEY_MEDIA_TYPE, "Audio");
    pw_properties_set(props, PW_KEY_MEDIA_CATEGORY, "DSP");
    pw_properties_set(props, PW_KEY_MEDIA_ROLE, "Effect");
    pw_properties_set(props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(props, PW_KEY_AUDIO_RATE, "48000");
    
    struct pw_loop *pl = pw_thread_loop_get_loop(mgr->loop);
    printf("DEBUG: pl=%p, loop=%p\n", (void*)pl, (void*)mgr->loop);
    
    // Use pw_filter_new with core (not loop) for more control
    mgr->filter = pw_filter_new(mgr->core, name, props);
    if (!mgr->filter) {
        fprintf(stderr, "DEBUG: pw_filter_new failed\n");
        free(fdata);
        cdata->result = -1;
        pw_properties_free(props);
        atomic_store(&cdata->done, true);
        return 0;
    }
    printf("DEBUG: filter created: %p\n", (void*)mgr->filter);
    
    // Add listener
    pw_filter_add_listener(mgr->filter, &fdata->listener, &filter_events, fdata);
    
    // Add ports BEFORE connecting - each port needs its own properties object
    // since pw_filter_add_port takes ownership of the properties
    
    // input_FL
    struct pw_properties *port_props = pw_properties_new(NULL, NULL);
    pw_properties_set(port_props, PW_KEY_FORMAT_DSP, "32 bit float");
    pw_properties_set(port_props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(port_props, PW_KEY_AUDIO_RATE, "48000");
    pw_properties_set(port_props, "audio.position", "FL");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "input_FL");
    fdata->in_ports[0].port_data = pw_filter_add_port(mgr->filter, PW_DIRECTION_INPUT, 
                                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0, 
                                                      port_props, NULL, 0);
    printf("DEBUG: input_FL port: %p\n", (void*)fdata->in_ports[0].port_data);
    
    // input_FR
    port_props = pw_properties_new(NULL, NULL);
    pw_properties_set(port_props, PW_KEY_FORMAT_DSP, "32 bit float");
    pw_properties_set(port_props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(port_props, PW_KEY_AUDIO_RATE, "48000");
    pw_properties_set(port_props, "audio.position", "FR");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "input_FR");
    fdata->in_ports[1].port_data = pw_filter_add_port(mgr->filter, PW_DIRECTION_INPUT,
                                                      PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                                      port_props, NULL, 0);
    printf("DEBUG: input_FR port: %p\n", (void*)fdata->in_ports[1].port_data);
    
    // output_FL
    port_props = pw_properties_new(NULL, NULL);
    pw_properties_set(port_props, PW_KEY_FORMAT_DSP, "32 bit float");
    pw_properties_set(port_props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(port_props, PW_KEY_AUDIO_RATE, "48000");
    pw_properties_set(port_props, "audio.position", "FL");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "output_FL");
    fdata->out_ports[0].port_data = pw_filter_add_port(mgr->filter, PW_DIRECTION_OUTPUT,
                                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                                       port_props, NULL, 0);
    printf("DEBUG: output_FL port: %p\n", (void*)fdata->out_ports[0].port_data);
    
    // output_FR
    port_props = pw_properties_new(NULL, NULL);
    pw_properties_set(port_props, PW_KEY_FORMAT_DSP, "32 bit float");
    pw_properties_set(port_props, PW_KEY_AUDIO_CHANNELS, "2");
    pw_properties_set(port_props, PW_KEY_AUDIO_RATE, "48000");
    pw_properties_set(port_props, "audio.position", "FR");
    pw_properties_set(port_props, PW_KEY_PORT_NAME, "output_FR");
    fdata->out_ports[1].port_data = pw_filter_add_port(mgr->filter, PW_DIRECTION_OUTPUT,
                                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
                                                       port_props, NULL, 0);
    printf("DEBUG: output_FR port: %p\n", (void*)fdata->out_ports[1].port_data);
    // Note: pw_filter_new and pw_filter_add_port take ownership of properties,
    // so we do NOT free props or port_props here
    printf("DEBUG: ports created\n");
    
    chain->filter = mgr->filter;
    chain->loop = mgr->loop;
    mgr->chain = chain;
    
    cdata->result = 0;
    printf("DEBUG: do_create_filter done, result=0\n");
    atomic_store(&cdata->done, true);
    return 0;
}

struct destroy_filter_data {
    pw_manager_t *mgr;
    int result;
    bool done;
};

static int do_destroy_filter(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) {
    (void)loop; (void)async; (void)seq; (void)size; (void)user_data;
    struct destroy_filter_data *ddata = (struct destroy_filter_data *)data;
    pw_manager_t *mgr = ddata->mgr;
    
    if (mgr->filter) {
        pw_filter_disconnect(mgr->filter);
        pw_filter_destroy(mgr->filter);
        mgr->filter = NULL;
    }
    
    if (mgr->chain) {
        mgr->chain->filter = NULL;
        mgr->chain = NULL;
    }
    
    ddata->result = 0;
    ddata->done = true;
    return 0;
}

pw_manager_t* pw_manager_new(void) {
    pw_manager_t *mgr = calloc(1, sizeof(pw_manager_t));
    if (!mgr) return NULL;
    
    pw_init(NULL, NULL);
    
    mgr->loop = pw_thread_loop_new("easyeffects-cli", NULL);
    if (!mgr->loop) {
        free(mgr);
        return NULL;
    }
    
    printf("DEBUG: Starting thread loop\n");
    pw_thread_loop_start(mgr->loop);
    
    struct pw_loop *pl = pw_thread_loop_get_loop(mgr->loop);
    mgr->context = pw_context_new(pl, NULL, 0);
    if (!mgr->context) {
        pw_thread_loop_stop(mgr->loop);
        pw_thread_loop_destroy(mgr->loop);
        free(mgr);
        return NULL;
    }
    
    pw_thread_loop_lock(mgr->loop);
    // Connect synchronously within the thread loop
    mgr->core = pw_context_connect(mgr->context, pw_properties_new(
        PW_KEY_APP_NAME, "easyeffects-cli",
        NULL), 0);
    pw_thread_loop_unlock(mgr->loop);
    if (!mgr->core) {
        pw_context_destroy(mgr->context);
        pw_thread_loop_stop(mgr->loop);
        pw_thread_loop_destroy(mgr->loop);
        free(mgr);
        return NULL;
    }
    
    // Add core listener to ensure proper context handling
    pw_core_add_listener(mgr->core, &mgr->core_listener, NULL, mgr);
    
    printf("DEBUG: pw_manager_new done\n");
    mgr->active = false;
    return mgr;
}

void pw_manager_free(pw_manager_t *mgr) {
    if (!mgr) return;
    
    if (mgr->filter) {
        struct destroy_filter_data data = {0};
        data.mgr = mgr;
        data.result = -1;
        data.done = false;
        
        pw_thread_loop_lock(mgr->loop);
        pw_loop_invoke(pw_thread_loop_get_loop(mgr->loop), do_destroy_filter, 0, &data, sizeof(data), true, NULL);
        while (!data.done) {
            pw_thread_loop_wait(mgr->loop);
        }
        pw_thread_loop_unlock(mgr->loop);
    }
    
    if (mgr->core) {
        pw_core_disconnect(mgr->core);
    }
    
    if (mgr->context) {
        pw_context_destroy(mgr->context);
    }
    
    if (mgr->loop) {
        pw_thread_loop_stop(mgr->loop);
        pw_thread_loop_destroy(mgr->loop);
    }
    
    pw_deinit();
    free(mgr);
}

int pw_manager_create_filter(pw_manager_t *mgr, const char *name, plugin_chain_t *chain) {
    if (!mgr || !chain) return -1;
    
    printf("DEBUG: pw_manager_create_filter called\n");
    struct create_filter_data *data = calloc(1, sizeof(struct create_filter_data));
    if (!data) return -1;
    data->mgr = mgr;
    data->name = name;
    data->chain = chain;
    data->result = -1;
    atomic_init(&data->done, false);
    
    printf("DEBUG: Locking thread loop\n");
    pw_thread_loop_lock(mgr->loop);
    printf("DEBUG: Invoking do_create_filter (async, lock held)\n");
    // Use async=true so callback is queued, but we hold lock so it won't run until we unlock
    pw_loop_invoke(pw_thread_loop_get_loop(mgr->loop), do_create_filter, 0, NULL, 0, true, data);
    printf("DEBUG: do_create_filter invoked, waiting for callback...\n");
    // Now wait with lock released
    int wait_count = 0;
    while (!atomic_load(&data->done)) {
        printf("DEBUG: Calling pw_thread_loop_wait (count=%d), data=%p, done_addr=%p, done_val=%d\n", wait_count++, (void*)data, (void*)&data->done, atomic_load(&data->done));
        pw_thread_loop_unlock(mgr->loop);
        pw_thread_loop_wait(mgr->loop);
        pw_thread_loop_lock(mgr->loop);
        printf("DEBUG: pw_thread_loop_wait returned, data.done=%d\n", atomic_load(&data->done));
        if (wait_count > 20) {
            printf("DEBUG: Too many waits, breaking\n");
            break;
        }
    }
    printf("DEBUG: do_create_filter done, result=%d\n", data->result);
    int result = data->result;
    free(data);
    pw_thread_loop_unlock(mgr->loop);
    
    return result;
}

struct activate_data {
    pw_manager_t *mgr;
    int result;
    atomic_bool done;
};

static int do_activate(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) {
    (void)loop; (void)async; (void)seq; (void)data; (void)size;
    struct activate_data *d = (struct activate_data *)user_data;
    printf("DEBUG: do_activate called, connecting filter...\n");
    if (!d->mgr->filter) {
        printf("DEBUG: ERROR - filter is NULL\n");
        d->result = -1;
        atomic_store(&d->done, true);
        return 0;
    }
    printf("DEBUG: About to call pw_filter_connect\n");
    d->result = pw_filter_connect(d->mgr->filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0);
    printf("DEBUG: pw_filter_connect returned %d\n", d->result);
    atomic_store(&d->done, true);
    return 0;
}

int pw_manager_activate(pw_manager_t *mgr) {
    if (!mgr || !mgr->filter) return -1;
    
    printf("DEBUG: pw_manager_activate called\n");
    struct activate_data *data = calloc(1, sizeof(struct activate_data));
    if (!data) return -1;
    data->mgr = mgr;
    data->result = -1;
    atomic_init(&data->done, false);
    
    printf("DEBUG: Locking thread loop\n");
    pw_thread_loop_lock(mgr->loop);
    printf("DEBUG: Invoking do_activate (async, lock held)\n");
    pw_loop_invoke(pw_thread_loop_get_loop(mgr->loop), do_activate, 0, NULL, 0, true, data);
    printf("DEBUG: Waiting for do_activate to complete\n");
    int wait_count = 0;
    while (!atomic_load(&data->done)) {
        printf("DEBUG: Calling pw_thread_loop_wait (count=%d)\n", wait_count++);
        pw_thread_loop_unlock(mgr->loop);
        pw_thread_loop_wait(mgr->loop);
        pw_thread_loop_lock(mgr->loop);
        if (wait_count > 20) {
            printf("DEBUG: Too many waits, breaking\n");
            break;
        }
    }
    printf("DEBUG: do_activate done, result=%d\n", data->result);
    int result = data->result;
    free(data);
    pw_thread_loop_unlock(mgr->loop);
    
    if (result < 0) return -1;
    
    mgr->active = true;
    return 0;
}

struct deactivate_data {
    pw_manager_t *mgr;
    int result;
    atomic_bool done;
};

static int do_deactivate(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) {
    (void)loop; (void)async; (void)seq; (void)data; (void)size;
    struct deactivate_data *d = (struct deactivate_data *)user_data;
    printf("DEBUG: do_deactivate called, disconnecting filter...\n");
    d->result = pw_filter_disconnect(d->mgr->filter);
    printf("DEBUG: pw_filter_disconnect returned %d\n", d->result);
    atomic_store(&d->done, true);
    return 0;
}

void pw_manager_deactivate(pw_manager_t *mgr) {
    if (!mgr || !mgr->filter) return;
    
    struct deactivate_data *data = calloc(1, sizeof(struct deactivate_data));
    if (!data) return;
    data->mgr = mgr;
    data->result = -1;
    atomic_init(&data->done, false);
    
    printf("DEBUG: Locking thread loop\n");
    pw_thread_loop_lock(mgr->loop);
    printf("DEBUG: Invoking do_deactivate (async, lock held)\n");
    pw_loop_invoke(pw_thread_loop_get_loop(mgr->loop), do_deactivate, 0, NULL, 0, true, data);
    printf("DEBUG: Waiting for do_deactivate to complete\n");
    int wait_count = 0;
    while (!atomic_load(&data->done)) {
        printf("DEBUG: Calling pw_thread_loop_wait (count=%d)\n", wait_count++);
        pw_thread_loop_unlock(mgr->loop);
        pw_thread_loop_wait(mgr->loop);
        pw_thread_loop_lock(mgr->loop);
        if (wait_count > 20) {
            printf("DEBUG: Too many waits, breaking\n");
            break;
        }
    }
    printf("DEBUG: do_deactivate done, result=%d\n", data->result);
    free(data);
    pw_thread_loop_unlock(mgr->loop);
    
    mgr->active = false;
}

struct connect_ports_data {
    pw_manager_t *mgr;
    const char *src_node;
    const char *src_port;
    const char *dst_node;
    const char *dst_port;
    int result;
    atomic_bool done;
};

static int do_connect_ports(struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) {
    (void)loop; (void)async; (void)seq; (void)data; (void)size;
    struct connect_ports_data *d = (struct connect_ports_data *)user_data;
    pw_manager_t *mgr = d->mgr;
    
    printf("DEBUG: do_connect_ports: %s:%s -> %s:%s\n", 
           d->src_node, d->src_port, d->dst_node, d->dst_port);
    
    // Use pw_core_create_object with link-factory to create links
    struct pw_properties *props = pw_properties_new(NULL, NULL);
    pw_properties_set(props, "link.output.node", d->src_node);
    pw_properties_set(props, "link.output.port", d->src_port);
    pw_properties_set(props, "link.input.node", d->dst_node);
    pw_properties_set(props, "link.input.port", d->dst_port);
    
    // Create link using the link-factory
    void *link = pw_core_create_object(mgr->core, "link-factory", "Link", 3, &props->dict, 0);
    if (link) {
        d->result = 0;
        printf("DEBUG: Link created successfully\n");
    } else {
        d->result = -1;
        fprintf(stderr, "DEBUG: Failed to create link\n");
    }
    pw_properties_free(props);
    
    atomic_store(&d->done, true);
    return 0;
}

int pw_manager_connect_ports(pw_manager_t *mgr, const char *src_node, const char *src_port,
                             const char *dst_node, const char *dst_port) {
    if (!mgr || !mgr->core) return -1;
    
    printf("Linking %s:%s -> %s:%s\n", src_node, src_port, dst_node, dst_port);
    
    struct connect_ports_data *data = calloc(1, sizeof(struct connect_ports_data));
    if (!data) return -1;
    data->mgr = mgr;
    data->src_node = src_node;
    data->src_port = src_port;
    data->dst_node = dst_node;
    data->dst_port = dst_port;
    data->result = -1;
    atomic_init(&data->done, false);
    
    pw_thread_loop_lock(mgr->loop);
    pw_loop_invoke(pw_thread_loop_get_loop(mgr->loop), do_connect_ports, 0, NULL, 0, true, data);
    
    int wait_count = 0;
    while (!atomic_load(&data->done)) {
        pw_thread_loop_unlock(mgr->loop);
        pw_thread_loop_wait(mgr->loop);
        pw_thread_loop_lock(mgr->loop);
        if (wait_count++ > 20) break;
    }
    
    int result = data->result;
    free(data);
    pw_thread_loop_unlock(mgr->loop);
    
    return result;
}

int pw_manager_connect_to_system(pw_manager_t *mgr, const char *input_node, const char *output_node) {
    if (!mgr || !mgr->filter) return -1;
    
    uint32_t filter_node_id = pw_filter_get_node_id(mgr->filter);
    printf("Filter node ID: %u\n", filter_node_id);
    
    // Note: With PipeWire 0.3+, the session manager (WirePlumber) should automatically
    // connect filter ports to default sink/source. Manual linking via link-factory
    // requires node IDs, not names, which is complex to implement.
    // For now, we rely on the session manager to make connections.
    // Users can use pw-cli or helvum to manually connect if needed.
    printf("Filter ports created. Session manager should auto-connect to default sink/source.\n");
    printf("Use 'pw-cli ls Node' to see nodes and 'pw-cli link <output-node> <input-node>' to connect manually.\n");
    
    return 0;
}

struct pw_loop* pw_manager_get_loop(pw_manager_t *mgr) {
    return mgr ? pw_thread_loop_get_loop(mgr->loop) : NULL;
}

struct pw_thread_loop* pw_manager_get_thread_loop(pw_manager_t *mgr) {
    return mgr ? mgr->loop : NULL;
}