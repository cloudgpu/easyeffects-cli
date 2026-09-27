#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <sys/stat.h>
#include <unistd.h>

#define PRESET_DIR "~/.config/easyeffects-cli/presets"

typedef struct {
    char plugin_uri[512];
    char plugin_name[256];
    char param_name[128];
    double value;
    char type[32];
} param_entry_t;

typedef struct {
    char name[256];
    char description[512];
    param_entry_t params[256];
    int param_count;
    char chain[1024];
} preset_t;

static void ensure_preset_dir() {
    char home[512];
    const char *home_env = getenv("HOME");
    if (home_env) {
        snprintf(home, sizeof(home), "%s/.config/easyeffects-cli/presets", home_env);
        mkdir(home, 0755);
    }
}

static char* preset_path(const char *name) {
    char *path = malloc(512);
    const char *home = getenv("HOME");
    snprintf(path, 512, "%s/.config/easyeffects-cli/presets/%s.json", home, name);
    return path;
}

int preset_save(const char *name, preset_t *preset) {
    ensure_preset_dir();
    
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "name", json_object_new_string(preset->name));
    json_object_object_add(jobj, "description", json_object_new_string(preset->description));
    json_object_object_add(jobj, "chain", json_object_new_string(preset->chain));
    
    json_object *jparams = json_object_new_array();
    for (int i = 0; i < preset->param_count; i++) {
        json_object *jparam = json_object_new_object();
        json_object_object_add(jparam, "plugin_uri", json_object_new_string(preset->params[i].plugin_uri));
        json_object_object_add(jparam, "plugin_name", json_object_new_string(preset->params[i].plugin_name));
        json_object_object_add(jparam, "param_name", json_object_new_string(preset->params[i].param_name));
        json_object_object_add(jparam, "value", json_object_new_double(preset->params[i].value));
        json_object_object_add(jparam, "type", json_object_new_string(preset->params[i].type));
        json_object_array_add(jparams, jparam);
    }
    json_object_object_add(jobj, "params", jparams);
    
    char *path = preset_path(name);
    FILE *f = fopen(path, "w");
    if (!f) {
        free(path);
        return -1;
    }
    
    fprintf(f, "%s\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));
    fclose(f);
    free(path);
    
    printf("Preset saved: %s\n", name);
    return 0;
}

int preset_load(const char *name, preset_t *preset) {
    char *path = preset_path(name);
    FILE *f = fopen(path, "r");
    if (!f) {
        free(path);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    
    json_object *jobj = json_tokener_parse(data);
    free(data);
    
    if (!jobj) {
        free(path);
        return -1;
    }
    
    json_object *jname, *jdesc, *jchain, *jparams;
    json_object_object_get_ex(jobj, "name", &jname);
    json_object_object_get_ex(jobj, "description", &jdesc);
    json_object_object_get_ex(jobj, "chain", &jchain);
    json_object_object_get_ex(jobj, "params", &jparams);
    
    strncpy(preset->name, json_object_get_string(jname), 255);
    strncpy(preset->description, json_object_get_string(jdesc), 511);
    strncpy(preset->chain, json_object_get_string(jchain), 1023);
    
    preset->param_count = json_object_array_length(jparams);
    for (int i = 0; i < preset->param_count; i++) {
        json_object *jparam = json_object_array_get_idx(jparams, i);
        json_object *tmp;
        
        json_object_object_get_ex(jparam, "plugin_uri", &tmp);
        strncpy(preset->params[i].plugin_uri, json_object_get_string(tmp), 511);
        
        json_object_object_get_ex(jparam, "plugin_name", &tmp);
        strncpy(preset->params[i].plugin_name, json_object_get_string(tmp), 255);
        
        json_object_object_get_ex(jparam, "param_name", &tmp);
        strncpy(preset->params[i].param_name, json_object_get_string(tmp), 127);
        
        json_object_object_get_ex(jparam, "value", &tmp);
        preset->params[i].value = json_object_get_double(tmp);
        
        json_object_object_get_ex(jparam, "type", &tmp);
        strncpy(preset->params[i].type, json_object_get_string(tmp), 31);
    }
    
    free(path);
    printf("Preset loaded: %s\n", name);
    return 0;
}

void preset_list() {
    ensure_preset_dir();
    const char *home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/easyeffects-cli/presets", home);
    
    printf("Available presets:\n");
    printf("==================\n");
    
    // Simple directory listing
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ls -1 %s 2>/dev/null", path);
    system(cmd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s preset <save|load|list> [name]\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "preset") == 0) {
        if (strcmp(argv[2], "list") == 0) {
            preset_list();
        } else if (strcmp(argv[2], "save") == 0 && argc >= 4) {
            preset_t p = {0};
            strncpy(p.name, argv[3], 255);
            preset_save(argv[3], &p);
        } else if (strcmp(argv[2], "load") == 0 && argc >= 4) {
            preset_t p = {0};
            preset_load(argv[3], &p);
        }
    }
    
    return 0;
}
