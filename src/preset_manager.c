#include "easyeffects_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

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
        json_object_put(jobj);
        return -1;
    }
    
    fprintf(f, "%s\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY));
    fclose(f);
    free(path);
    json_object_put(jobj);
    
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
    
    if (jname) strncpy(preset->name, json_object_get_string(jname), 255);
    if (jdesc) strncpy(preset->description, json_object_get_string(jdesc), 511);
    if (jchain) strncpy(preset->chain, json_object_get_string(jchain), 1023);
    
    if (jparams) {
        preset->param_count = json_object_array_length(jparams);
        for (int i = 0; i < preset->param_count && i < 256; i++) {
            json_object *jparam = json_object_array_get_idx(jparams, i);
            json_object *tmp;
            
            json_object_object_get_ex(jparam, "plugin_uri", &tmp);
            if (tmp) strncpy(preset->params[i].plugin_uri, json_object_get_string(tmp), 511);
            
            json_object_object_get_ex(jparam, "plugin_name", &tmp);
            if (tmp) strncpy(preset->params[i].plugin_name, json_object_get_string(tmp), 255);
            
            json_object_object_get_ex(jparam, "param_name", &tmp);
            if (tmp) strncpy(preset->params[i].param_name, json_object_get_string(tmp), 127);
            
            json_object_object_get_ex(jparam, "value", &tmp);
            if (tmp) preset->params[i].value = json_object_get_double(tmp);
            
            json_object_object_get_ex(jparam, "type", &tmp);
            if (tmp) strncpy(preset->params[i].type, json_object_get_string(tmp), 31);
        }
    }
    
    free(path);
    json_object_put(jobj);
    printf("Preset loaded: %s\n", name);
    return 0;
}

void preset_list(void) {
    ensure_preset_dir();
    const char *home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/easyeffects-cli/presets", home);
    
    printf("Available presets:\n");
    printf("==================\n");
    
    DIR *dir = opendir(path);
    if (!dir) {
        printf("  (none)\n");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            printf("  %s\n", entry->d_name);
        }
    }
    closedir(dir);
}

int preset_delete(const char *name) {
    char *path = preset_path(name);
    int result = remove(path);
    free(path);
    if (result == 0) {
        printf("Preset deleted: %s\n", name);
    }
    return result;
}