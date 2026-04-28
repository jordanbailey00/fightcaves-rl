/*
 * binding.c — PufferLib 4.0 binding for Fight Caves environment.
 *
 * Defines the macros PufferLib needs, includes vecenv.h, and implements
 * the my_init/my_log hooks for config parsing and stat logging.
 */

#include "fight_caves.h"
#include "fc_reward.h"

#define OBS_SIZE FC_PUFFER_OBS_SIZE
#define OBS_TENSOR_T FloatTensor
#define NUM_ATNS FC_PUFFER_NUM_ATNS
#define ACT_SIZES {17, 9, 5, 3, 2}
#define OBS_TYPE FLOAT
#define ACT_TYPE DOUBLE

#define Env FightCaves
#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;  /* Fight Caves is single-agent */
    FcRewardParams defaults = fc_reward_default_params();

    /* Reward shaping weights (from config/fight_caves.ini [env] section) */
    DictItem* item;
    item = dict_get_unsafe(kwargs, "w_damage_dealt");
    env->w_damage_dealt = item ? (float)item->value : defaults.w_damage_dealt;
    item = dict_get_unsafe(kwargs, "w_damage_taken");
    env->w_damage_taken = item ? (float)item->value : defaults.w_damage_taken;
    item = dict_get_unsafe(kwargs, "w_npc_kill");
    env->w_npc_kill = item ? (float)item->value : defaults.w_npc_kill;
    item = dict_get_unsafe(kwargs, "w_wave_clear");
    env->w_wave_clear = item ? (float)item->value : defaults.w_wave_clear;
    item = dict_get_unsafe(kwargs, "w_jad_kill");
    env->w_jad_kill = item ? (float)item->value : defaults.w_jad_kill;
    item = dict_get_unsafe(kwargs, "w_player_death");
    env->w_player_death = item ? (float)item->value : defaults.w_player_death;
    item = dict_get_unsafe(kwargs, "w_correct_jad_prayer");
    env->w_correct_jad_prayer = item ? (float)item->value : defaults.w_correct_jad_prayer;
    item = dict_get_unsafe(kwargs, "w_correct_danger_prayer");
    env->w_correct_danger_prayer = item ? (float)item->value : defaults.w_correct_danger_prayer;
    item = dict_get_unsafe(kwargs, "w_invalid_action");
    env->w_invalid_action = item ? (float)item->value : defaults.w_invalid_action;
    item = dict_get_unsafe(kwargs, "w_tick_penalty");
    env->w_tick_penalty = item ? (float)item->value : defaults.w_tick_penalty;

    /* Configurable shaping terms */
    item = dict_get_unsafe(kwargs, "shape_food_waste_scale");
    env->shape_food_waste_scale = item ? (float)item->value : defaults.shape_food_waste_scale;
    item = dict_get_unsafe(kwargs, "shape_pot_waste_scale");
    env->shape_pot_waste_scale = item ? (float)item->value : defaults.shape_pot_waste_scale;
    item = dict_get_unsafe(kwargs, "shape_wrong_prayer_penalty");
    env->shape_wrong_prayer_penalty = item ? (float)item->value : defaults.shape_wrong_prayer_penalty;
    item = dict_get_unsafe(kwargs, "shape_npc_melee_penalty");
    env->shape_npc_melee_penalty = item ? (float)item->value : defaults.shape_npc_melee_penalty;
    item = dict_get_unsafe(kwargs, "shape_wasted_attack_penalty");
    env->shape_wasted_attack_penalty = item ? (float)item->value : defaults.shape_wasted_attack_penalty;
    item = dict_get_unsafe(kwargs, "shape_kiting_reward");
    env->shape_kiting_reward = item ? (float)item->value : defaults.shape_kiting_reward;
    item = dict_get_unsafe(kwargs, "shape_safespot_attack_reward");
    env->shape_safespot_attack_reward = item ? (float)item->value : defaults.shape_safespot_attack_reward;
    item = dict_get_unsafe(kwargs, "shape_unnecessary_prayer_penalty");
    env->shape_unnecessary_prayer_penalty = item ? (float)item->value : defaults.shape_unnecessary_prayer_penalty;
    item = dict_get_unsafe(kwargs, "shape_wave_stall_base_penalty");
    env->shape_wave_stall_base_penalty = item ? (float)item->value : defaults.shape_wave_stall_base_penalty;
    item = dict_get_unsafe(kwargs, "shape_wave_stall_cap");
    env->shape_wave_stall_cap = item ? (float)item->value : defaults.shape_wave_stall_cap;
    item = dict_get_unsafe(kwargs, "shape_jad_heal_penalty");
    env->shape_jad_heal_penalty = item ? (float)item->value : defaults.shape_jad_heal_penalty;
    item = dict_get_unsafe(kwargs, "shape_resource_threat_window");
    env->shape_resource_threat_window = item ? (int)item->value : defaults.shape_resource_threat_window;
    item = dict_get_unsafe(kwargs, "shape_kiting_min_dist");
    env->shape_kiting_min_dist = item ? (int)item->value : defaults.shape_kiting_min_dist;
    item = dict_get_unsafe(kwargs, "shape_kiting_max_dist");
    env->shape_kiting_max_dist = item ? (int)item->value : defaults.shape_kiting_max_dist;
    item = dict_get_unsafe(kwargs, "shape_wave_stall_start");
    env->shape_wave_stall_start = item ? (int)item->value : defaults.shape_wave_stall_start;
    item = dict_get_unsafe(kwargs, "shape_wave_stall_ramp_interval");
    env->shape_wave_stall_ramp_interval = item ? (int)item->value : defaults.shape_wave_stall_ramp_interval;

    /* Obs ablation flags (default 0 — i.e. no ablation, full obs).
     * See fc_apply_obs_ablation in fc-core/src/fc_state.c for what each zeroes. */
    item = dict_get_unsafe(kwargs, "obs_ablate_npc_distance");
    env->obs_ablate_npc_distance = item ? (int)item->value : 0;
    item = dict_get_unsafe(kwargs, "obs_ablate_incoming_aggregates");
    env->obs_ablate_incoming_aggregates = item ? (int)item->value : 0;
    item = dict_get_unsafe(kwargs, "obs_ablate_npc_valid");
    env->obs_ablate_npc_valid = item ? (int)item->value : 0;

    /* Initialize game state */
    env->seed_counter = 0;
    fc_init(&env->state);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wave_reached", log->wave_reached);
    /* Globals — bypass PufferLib Log struct, averaged here */
    extern float g_max_wave_ever;
    extern float g_most_npcs_ever;
    extern float g_sum_prayer_uptime_melee;
    extern float g_sum_prayer_uptime_range, g_sum_prayer_uptime_magic;
    extern float g_sum_correct_blocks;
    extern float g_sum_wrong_prayer_hits, g_sum_no_prayer_hits;
    extern float g_sum_prayer_switches, g_sum_damage_blocked, g_sum_damage_taken;
    extern float g_sum_attack_when_ready;
    extern float g_sum_pots_used, g_sum_avg_prayer_on_pot;
    extern float g_sum_food_eaten, g_sum_avg_hp_on_food;
    extern float g_sum_food_wasted, g_sum_pots_wasted, g_n_analytics;
    extern float g_sum_tokxil_melee_ticks, g_sum_ketzek_melee_ticks;
    extern float g_sum_max_wave_ticks, g_sum_max_wave_ticks_wave;
    extern float g_sum_reached_wave_63, g_sum_jad_kill_rate;
    dict_set(out, "max_wave", g_max_wave_ever);
    dict_set(out, "most_npcs_slayed", g_most_npcs_ever);
    if (g_n_analytics > 0) {
        dict_set(out, "prayer_uptime_melee", g_sum_prayer_uptime_melee / g_n_analytics);
        dict_set(out, "prayer_uptime_range", g_sum_prayer_uptime_range / g_n_analytics);
        dict_set(out, "prayer_uptime_magic", g_sum_prayer_uptime_magic / g_n_analytics);
        dict_set(out, "correct_prayer", g_sum_correct_blocks / g_n_analytics);
        dict_set(out, "wrong_prayer_hits", g_sum_wrong_prayer_hits / g_n_analytics);
        dict_set(out, "no_prayer_hits", g_sum_no_prayer_hits / g_n_analytics);
        dict_set(out, "prayer_switches", g_sum_prayer_switches / g_n_analytics);
        dict_set(out, "damage_blocked", g_sum_damage_blocked / g_n_analytics);
        dict_set(out, "dmg_taken_avg", g_sum_damage_taken / g_n_analytics);
        dict_set(out, "attack_when_ready_rate", g_sum_attack_when_ready / g_n_analytics);
        dict_set(out, "pots_used", g_sum_pots_used / g_n_analytics);
        dict_set(out, "avg_prayer_on_pot", g_sum_avg_prayer_on_pot / g_n_analytics);
        dict_set(out, "food_eaten", g_sum_food_eaten / g_n_analytics);
        dict_set(out, "avg_hp_on_food", g_sum_avg_hp_on_food / g_n_analytics);
        dict_set(out, "food_wasted", g_sum_food_wasted / g_n_analytics);
        dict_set(out, "pots_wasted", g_sum_pots_wasted / g_n_analytics);
        dict_set(out, "tokxil_melee_ticks", g_sum_tokxil_melee_ticks / g_n_analytics);
        dict_set(out, "ketzek_melee_ticks", g_sum_ketzek_melee_ticks / g_n_analytics);
        dict_set(out, "max_wave_ticks", g_sum_max_wave_ticks / g_n_analytics);
        dict_set(out, "max_wave_ticks_wave", g_sum_max_wave_ticks_wave / g_n_analytics);
        dict_set(out, "reached_wave_63", g_sum_reached_wave_63 / g_n_analytics);
        dict_set(out, "jad_kill_rate", g_sum_jad_kill_rate / g_n_analytics);
        /* Per-reward-channel per-episode averages (total reward and fire count).
         * Keys must use static storage because dict_set stores the pointer (see
         * vecenv.h:dict_set) — stack-local char arrays would leave dangling
         * pointers once the loop iteration ends, corrupting the dict. */
        extern float g_sum_rwd[FC_CH_COUNT];
        extern float g_fires_rwd[FC_CH_COUNT];
        static char rwd_keys_total[FC_CH_COUNT][48];
        static char rwd_keys_fires[FC_CH_COUNT][48];
        static int rwd_keys_built = 0;
        if (!rwd_keys_built) {
            for (int i = 0; i < FC_CH_COUNT; i++) {
                snprintf(rwd_keys_total[i], 48, "rwd_%s_total", FC_CH_NAMES[i]);
                snprintf(rwd_keys_fires[i], 48, "rwd_%s_fires", FC_CH_NAMES[i]);
            }
            rwd_keys_built = 1;
        }
        for (int i = 0; i < FC_CH_COUNT; i++) {
            dict_set(out, rwd_keys_total[i], g_sum_rwd[i] / g_n_analytics);
            dict_set(out, rwd_keys_fires[i], g_fires_rwd[i] / g_n_analytics);
            g_sum_rwd[i] = 0;
            g_fires_rwd[i] = 0;
        }
        g_sum_prayer_uptime_melee = 0;
        g_sum_prayer_uptime_range = 0; g_sum_prayer_uptime_magic = 0;
        g_sum_correct_blocks = 0;
        g_sum_wrong_prayer_hits = 0; g_sum_no_prayer_hits = 0;
        g_sum_prayer_switches = 0; g_sum_damage_blocked = 0;
        g_sum_damage_taken = 0; g_sum_attack_when_ready = 0;
        g_sum_pots_used = 0;
        g_sum_avg_prayer_on_pot = 0; g_sum_food_eaten = 0;
        g_sum_avg_hp_on_food = 0; g_sum_food_wasted = 0;
        g_sum_pots_wasted = 0; g_sum_tokxil_melee_ticks = 0;
        g_sum_ketzek_melee_ticks = 0; g_sum_max_wave_ticks = 0;
        g_sum_max_wave_ticks_wave = 0; g_sum_reached_wave_63 = 0;
        g_sum_jad_kill_rate = 0;
        g_n_analytics = 0;
    }
}
