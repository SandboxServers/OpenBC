#include "openbc/ship_data.h"
#include "openbc/json_parse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: copy string from JSON value into fixed buffer */
static void copy_str(char *dst, size_t dst_size, const json_value_t *val)
{
    const char *s = json_string(val);
    if (s) {
        strncpy(dst, s, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/* Helper: read a JSON array of floats into a C array */
static void read_float_array(const json_value_t *arr, f32 *out, int max_count)
{
    if (!arr || arr->type != JSON_ARRAY) return;
    size_t n = json_array_len(arr);
    if ((int)n > max_count) n = (size_t)max_count;
    for (size_t i = 0; i < n; i++) {
        out[i] = (f32)json_number(json_array_get(arr, i));
    }
}

/* Helper: read bc_vec3_t from JSON array [x, y, z] */
static bc_vec3_t read_vec3(const json_value_t *arr)
{
    bc_vec3_t v = {0, 0, 0};
    if (!arr || arr->type != JSON_ARRAY || json_array_len(arr) < 3) return v;
    v.x = (f32)json_number(json_array_get(arr, 0));
    v.y = (f32)json_number(json_array_get(arr, 1));
    v.z = (f32)json_number(json_array_get(arr, 2));
    return v;
}

static bool load_subsystem(bc_subsystem_def_t *ss, const json_value_t *obj)
{
    memset(ss, 0, sizeof(*ss));
    copy_str(ss->name, sizeof(ss->name), json_get(obj, "name"));
    copy_str(ss->type, sizeof(ss->type), json_get(obj, "type"));
    ss->position = read_vec3(json_get(obj, "position"));
    ss->radius = (f32)json_number(json_get(obj, "radius"));
    ss->max_condition = (f32)json_number(json_get(obj, "max_condition"));
    ss->disabled_pct = (f32)json_number(json_get(obj, "disabled_pct"));
    ss->is_critical = json_bool(json_get(obj, "is_critical"));
    ss->is_targetable = json_bool(json_get(obj, "is_targetable"));
    ss->repair_complexity = (f32)json_number(json_get(obj, "repair_complexity"));

    /* Weapon fields (phaser, pulse, tractor) */
    ss->max_damage = (f32)json_number(json_get(obj, "max_damage"));
    ss->max_charge = (f32)json_number(json_get(obj, "max_charge"));
    ss->min_firing_charge = (f32)json_number(json_get(obj, "min_firing_charge"));
    ss->recharge_rate = (f32)json_number(json_get(obj, "recharge_rate"));
    ss->discharge_rate = (f32)json_number(json_get(obj, "discharge_rate"));
    ss->max_damage_distance = (f32)json_number(json_get(obj, "max_damage_distance"));
    ss->weapon_id = (u8)json_int(json_get(obj, "weapon_id"));

    /* Orientation */
    ss->forward = read_vec3(json_get(obj, "forward"));
    ss->up = read_vec3(json_get(obj, "up"));
    read_float_array(json_get(obj, "arc_width"), ss->arc_width, 2);
    read_float_array(json_get(obj, "arc_height"), ss->arc_height, 2);

    /* Torpedo tube */
    ss->reload_delay = (f32)json_number(json_get(obj, "reload_delay"));
    ss->max_ready = json_int(json_get(obj, "max_ready"));
    ss->immediate_delay = (f32)json_number(json_get(obj, "immediate_delay"));
    ss->direction = read_vec3(json_get(obj, "direction"));

    /* Tractor */
    ss->normal_power = (f32)json_number(json_get(obj, "normal_power"));

    /* Cloak */
    ss->cloak_strength = (f32)json_number(json_get(obj, "cloak_strength"));

    /* Repair */
    ss->max_repair_points = (f32)json_number(json_get(obj, "max_repair_points"));
    ss->num_repair_teams = json_int(json_get(obj, "num_repair_teams"));

    ss->parent_idx = -1; /* set during serialization list loading */

    return true;
}

/* Find a subsystem in the flat array by name. Returns index or -1. */
static int find_subsys_by_name(const bc_ship_class_t *ship, const char *name)
{
    for (int i = 0; i < ship->subsystem_count; i++) {
        if (strcmp(ship->subsystems[i].name, name) == 0) return i;
    }
    return -1;
}

/* Parse format string ("base", "powered", "power") to enum. */
static u8 parse_ss_format(const json_value_t *val)
{
    const char *s = json_string(val);
    if (!s) return BC_SS_FORMAT_BASE;
    if (strcmp(s, "powered") == 0) return BC_SS_FORMAT_POWERED;
    if (strcmp(s, "power") == 0)   return BC_SS_FORMAT_POWER;
    return BC_SS_FORMAT_BASE;
}

/* Load the hierarchical serialization list from JSON.
 * Matches entries/children to the flat subsystem array by name.
 * New container entries get HP slots beyond subsystem_count. */
static void load_serialization_list(bc_ship_class_t *ship, const json_value_t *arr)
{
    bc_ss_list_t *sl = &ship->ser_list;
    memset(sl, 0, sizeof(*sl));
    sl->reactor_entry_idx = -1;

    if (!arr || arr->type != JSON_ARRAY) {
        sl->total_hp_slots = ship->subsystem_count;
        return;
    }

    size_t n = json_array_len(arr);
    if ((int)n > BC_SS_MAX_ENTRIES) n = BC_SS_MAX_ENTRIES;
    sl->count = (int)n;

    /* Next available HP slot for containers not in the flat array */
    int next_hp_slot = ship->subsystem_count;

    for (size_t i = 0; i < n; i++) {
        const json_value_t *entry_obj = json_array_get(arr, i);
        bc_ss_entry_t *e = &sl->entries[i];
        memset(e, 0, sizeof(*e));

        e->format = parse_ss_format(json_get(entry_obj, "format"));
        e->max_condition = (f32)json_number(json_get(entry_obj, "max_condition"));
        e->normal_power = (f32)json_number(json_get(entry_obj, "normal_power"));

        /* Match entry name to flat subsystem array */
        const char *ename = json_string(json_get(entry_obj, "name"));
        int flat_idx = ename ? find_subsys_by_name(ship, ename) : -1;
        if (flat_idx >= 0) {
            e->hp_index = flat_idx;
        } else {
            /* Container not in flat array — allocate a new HP slot */
            e->hp_index = next_hp_slot;
            if (next_hp_slot < BC_MAX_SUBSYSTEMS) next_hp_slot++;
        }

        /* Track reactor entry */
        if (e->format == BC_SS_FORMAT_POWER) {
            sl->reactor_entry_idx = (int)i;
        }

        /* Children */
        const json_value_t *children = json_get(entry_obj, "children");
        if (children && children->type == JSON_ARRAY) {
            size_t cn = json_array_len(children);
            if ((int)cn > BC_SS_MAX_CHILDREN) cn = BC_SS_MAX_CHILDREN;
            e->child_count = (int)cn;

            for (size_t c = 0; c < cn; c++) {
                const json_value_t *child_obj = json_array_get(children, c);
                const char *cname = json_string(json_get(child_obj, "name"));
                int cidx = cname ? find_subsys_by_name(ship, cname) : -1;
                if (cidx >= 0) {
                    e->child_hp_index[c] = cidx;
                    e->child_max_condition[c] = ship->subsystems[cidx].max_condition;
                    /* Set parent_idx on the child subsystem */
                    ship->subsystems[cidx].parent_idx = e->hp_index;
                } else {
                    /* Child not found — allocate slot (shouldn't happen with correct data) */
                    e->child_hp_index[c] = next_hp_slot;
                    e->child_max_condition[c] = (f32)json_number(
                        json_get(child_obj, "max_condition"));
                    if (next_hp_slot < BC_MAX_SUBSYSTEMS) next_hp_slot++;
                }
            }
        }
    }

    sl->total_hp_slots = next_hp_slot;
}

static bool load_ship(bc_ship_class_t *ship, const json_value_t *obj)
{
    memset(ship, 0, sizeof(*ship));
    copy_str(ship->name, sizeof(ship->name), json_get(obj, "name"));
    ship->species_id = (u16)json_int(json_get(obj, "species_id"));
    copy_str(ship->faction, sizeof(ship->faction), json_get(obj, "faction"));
    ship->hull_hp = (f32)json_number(json_get(obj, "hull_hp"));
    ship->mass = (f32)json_number(json_get(obj, "mass"));
    ship->rotational_inertia = (f32)json_number(json_get(obj, "rotational_inertia"));
    ship->max_speed = (f32)json_number(json_get(obj, "max_speed"));
    ship->max_accel = (f32)json_number(json_get(obj, "max_accel"));
    ship->max_angular_accel = (f32)json_number(json_get(obj, "max_angular_accel"));
    ship->max_angular_velocity = (f32)json_number(json_get(obj, "max_angular_velocity"));
    read_float_array(json_get(obj, "shield_hp"), ship->shield_hp, BC_MAX_SHIELD_FACINGS);
    read_float_array(json_get(obj, "shield_recharge"), ship->shield_recharge, BC_MAX_SHIELD_FACINGS);
    ship->can_cloak = json_bool(json_get(obj, "can_cloak"));
    ship->has_tractor = json_bool(json_get(obj, "has_tractor"));
    ship->torpedo_tubes = (u8)json_int(json_get(obj, "torpedo_tubes"));
    ship->phaser_banks = (u8)json_int(json_get(obj, "phaser_banks"));
    ship->pulse_weapons = (u8)json_int(json_get(obj, "pulse_weapons"));
    ship->tractor_beams = (u8)json_int(json_get(obj, "tractor_beams"));
    ship->max_repair_points = (f32)json_number(json_get(obj, "max_repair_points"));
    ship->num_repair_teams = json_int(json_get(obj, "num_repair_teams"));
    ship->damage_radius_multiplier = 1.0f;
    ship->damage_falloff_multiplier = 1.0f;

    /* Subsystems */
    json_value_t *subs = json_get(obj, "subsystems");
    if (subs && subs->type == JSON_ARRAY) {
        size_t n = json_array_len(subs);
        if ((int)n > BC_MAX_SUBSYSTEMS) n = BC_MAX_SUBSYSTEMS;
        ship->subsystem_count = (int)n;
        for (size_t i = 0; i < n; i++) {
            load_subsystem(&ship->subsystems[i], json_array_get(subs, i));
        }
    }

    /* Compute bounding extent from subsystem positions */
    {
        f32 max_dist = 0.0f;
        for (int i = 0; i < ship->subsystem_count; i++) {
            bc_vec3_t p = ship->subsystems[i].position;
            f32 d = sqrtf(p.x*p.x + p.y*p.y + p.z*p.z);
            if (d > max_dist) max_dist = d;
        }
        ship->bounding_extent = max_dist > 0.0f ? max_dist : 1.0f;
    }

    /* Serialization list (must be after subsystems are loaded) */
    load_serialization_list(ship, json_get(obj, "serialization_list"));

    /* Power plant parameters */
    ship->power_output = (f32)json_number(json_get(obj, "power_output"));
    ship->main_battery_limit = (f32)json_number(json_get(obj, "main_battery_limit"));
    ship->backup_battery_limit = (f32)json_number(json_get(obj, "backup_battery_limit"));
    ship->main_conduit_capacity = (f32)json_number(json_get(obj, "main_conduit_capacity"));
    ship->backup_conduit_capacity = (f32)json_number(json_get(obj, "backup_conduit_capacity"));

    return true;
}

static bool load_projectile(bc_projectile_def_t *proj, const json_value_t *obj)
{
    memset(proj, 0, sizeof(*proj));
    copy_str(proj->name, sizeof(proj->name), json_get(obj, "name"));
    copy_str(proj->script, sizeof(proj->script), json_get(obj, "script"));
    proj->net_type_id = (u8)json_int(json_get(obj, "net_type_id"));
    proj->damage = (f32)json_number(json_get(obj, "damage"));
    proj->launch_speed = (f32)json_number(json_get(obj, "launch_speed"));
    proj->power_cost = (f32)json_number(json_get(obj, "power_cost"));
    proj->guidance_lifetime = (f32)json_number(json_get(obj, "guidance_lifetime"));
    proj->max_angular_accel = (f32)json_number(json_get(obj, "max_angular_accel"));
    proj->lifetime = (f32)json_number(json_get(obj, "lifetime"));
    proj->damage_radius_factor = (f32)json_number(json_get(obj, "damage_radius_factor"));
    return true;
}

bool bc_registry_load(bc_game_registry_t *reg, const char *path)
{
    memset(reg, 0, sizeof(*reg));

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) { fclose(f); return false; }

    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return false; }

    size_t nread = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[nread] = '\0';

    json_value_t *root = json_parse(text);
    free(text);
    if (!root) return false;

    /* Ships */
    json_value_t *ships = json_get(root, "ships");
    if (ships && ships->type == JSON_ARRAY) {
        size_t n = json_array_len(ships);
        if ((int)n > BC_MAX_SHIPS) n = BC_MAX_SHIPS;
        reg->ship_count = (int)n;
        for (size_t i = 0; i < n; i++) {
            load_ship(&reg->ships[i], json_array_get(ships, i));
        }
    }

    /* Projectiles */
    json_value_t *projs = json_get(root, "projectiles");
    if (projs && projs->type == JSON_ARRAY) {
        size_t n = json_array_len(projs);
        if ((int)n > BC_MAX_PROJECTILES) n = BC_MAX_PROJECTILES;
        reg->projectile_count = (int)n;
        for (size_t i = 0; i < n; i++) {
            load_projectile(&reg->projectiles[i], json_array_get(projs, i));
        }
    }

    json_free(root);
    reg->loaded = true;
    return true;
}

const bc_ship_class_t *bc_registry_get_ship(const bc_game_registry_t *reg, int index)
{
    if (!reg || index < 0 || index >= reg->ship_count) return NULL;
    return &reg->ships[index];
}

const bc_ship_class_t *bc_registry_find_ship(const bc_game_registry_t *reg, u16 species_id)
{
    if (!reg) return NULL;
    for (int i = 0; i < reg->ship_count; i++) {
        if (reg->ships[i].species_id == species_id) return &reg->ships[i];
    }
    return NULL;
}

int bc_registry_find_ship_index(const bc_game_registry_t *reg, u16 species_id)
{
    if (!reg) return -1;
    for (int i = 0; i < reg->ship_count; i++) {
        if (reg->ships[i].species_id == species_id) return i;
    }
    return -1;
}

const bc_projectile_def_t *bc_registry_get_projectile(const bc_game_registry_t *reg, u8 net_type_id)
{
    if (!reg) return NULL;
    for (int i = 0; i < reg->projectile_count; i++) {
        if (reg->projectiles[i].net_type_id == net_type_id) return &reg->projectiles[i];
    }
    return NULL;
}
