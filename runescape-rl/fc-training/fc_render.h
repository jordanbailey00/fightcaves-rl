/*
 * fc_render.h — PufferLib c_render() implementation for Fight Caves.
 *
 * Self-contained Raylib rendering for eval mode. Loads terrain, objects,
 * and NPC models through the shared Fight Caves asset resolver. Provides camera controls, entity
 * rendering, HP/prayer bars, and a header overlay.
 *
 * Included by fight_caves.h when FC_RENDER is defined.
 * All functions are static to avoid linker conflicts.
 */

#ifndef FC_RENDER_H
#define FC_RENDER_H

#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Asset loaders from fc-viewer/src/ (header-only, raylib-dependent) */
#include "fc_terrain_loader.h"
#include "fc_objects_loader.h"
#include "fc_npc_models.h"

/* ======================================================================== */
/* Constants                                                                 */
/* ======================================================================== */

#define FCR_WIN_W       1280
#define FCR_WIN_H       800
#define FCR_HEADER_H    40
#define FCR_PANEL_W     200
#define FCR_MAX_HITSPLATS 32

/* Colors */
#define FCR_COL_BG        CLITERAL(Color){ 20, 20, 25, 255 }
#define FCR_COL_HEADER    CLITERAL(Color){ 30, 30, 40, 255 }
#define FCR_COL_PANEL     CLITERAL(Color){ 40, 35, 30, 255 }
#define FCR_COL_TEXT_Y    CLITERAL(Color){ 255, 255, 0, 255 }
#define FCR_COL_TEXT_W    CLITERAL(Color){ 255, 255, 255, 255 }
#define FCR_COL_TEXT_DIM  CLITERAL(Color){ 130, 130, 140, 255 }
#define FCR_COL_TEXT_SH   CLITERAL(Color){ 0, 0, 0, 255 }
#define FCR_COL_HP_G      CLITERAL(Color){ 30, 255, 30, 255 }
#define FCR_COL_HP_R      CLITERAL(Color){ 120, 0, 0, 255 }
#define FCR_COL_PRAY_B    CLITERAL(Color){ 60, 120, 220, 255 }
#define FCR_COL_PRAY_BG   CLITERAL(Color){ 20, 20, 60, 255 }
#define FCR_COL_PLAYER    CLITERAL(Color){ 80, 140, 255, 255 }
#define FCR_COL_TARGET    CLITERAL(Color){ 255, 255, 0, 180 }

static const Color FCR_NPC_COLORS[] = {
    {128,128,128,255}, /* 0: none */
    {180,160,60,255},  /* 1: Tz-Kih */
    {100,180,60,255},  /* 2: Tz-Kek */
    {80,150,50,255},   /* 3: Tz-Kek small */
    {60,60,200,255},   /* 4: Tok-Xil */
    {200,100,60,255},  /* 5: Yt-MejKot */
    {160,40,160,255},  /* 6: Ket-Zek */
    {200,40,40,255},   /* 7: TzTok-Jad */
    {60,200,200,255},  /* 8: Yt-HurKot */
};

static const char* FCR_NPC_NAMES[] = {
    "???", "Tz-Kih", "Tz-Kek", "Tz-Kek(s)", "Tok-Xil",
    "Yt-MejKot", "Ket-Zek", "TzTok-Jad", "Yt-HurKot"
};

/* ======================================================================== */
/* Hitsplat visual effect                                                    */
/* ======================================================================== */

typedef struct {
    int active;
    float world_x, world_y, world_z;
    int damage;       /* tenths */
    int frames_left;  /* 60 = 1 second at 60fps */
} FcrHitsplat;

/* ======================================================================== */
/* Render state — persists across c_render() calls                           */
/* ======================================================================== */

typedef struct {
    int initialized;

    /* Assets */
    TerrainMesh* terrain;
    ObjectMesh* objects;
    NpcModelSet* npc_models;
    NpcModelSet* player_model;

    /* Camera */
    Camera3D camera;
    float cam_yaw, cam_pitch, cam_dist;

    /* Controls */
    int tps;          /* game ticks per second (1-30) */
    int paused;

    /* Entity snapshot for rendering */
    FcRenderEntity entities[FC_MAX_RENDER_ENTITIES];
    int entity_count;

    /* Previous state for hitsplat detection */
    int prev_player_hp;
    int prev_npc_hp[FC_MAX_NPCS];

    /* Hitsplats */
    FcrHitsplat hitsplats[FCR_MAX_HITSPLATS];
} FcrState;

/* ======================================================================== */
/* Helpers                                                                   */
/* ======================================================================== */

static void fcr_text_s(const char* t, int x, int y, int sz, Color c) {
    DrawText(t, x+1, y+1, sz, FCR_COL_TEXT_SH);
    DrawText(t, x, y, sz, c);
}

static float fcr_ground_y(FcrState* rs, int tx, int ty) {
    if (rs->terrain && rs->terrain->loaded)
        return terrain_height_at(rs->terrain, tx, ty) + 0.1f;
    return 0.0f;
}

/* ======================================================================== */
/* Initialization                                                            */
/* ======================================================================== */

static void fcr_init(FcrState* rs) {
    memset(rs, 0, sizeof(*rs));

    InitWindow(FCR_WIN_W, FCR_WIN_H, "Fight Caves — Policy Replay");
    SetTargetFPS(60);

    /* Camera defaults */
    rs->cam_yaw = 0.0f;
    rs->cam_pitch = 0.6f;
    rs->cam_dist = 50.0f;
    rs->camera.up = (Vector3){0, 1, 0};
    rs->camera.fovy = 32.0f;
    rs->camera.projection = CAMERA_PERSPECTIVE;

    rs->tps = 2;
    rs->paused = 0;

    /* Load terrain */
    if (fc_asset_exists("fightcaves.terrain")) {
        rs->terrain = terrain_load("fightcaves.terrain");
        if (rs->terrain && rs->terrain->loaded)
            terrain_offset(rs->terrain, 2368, 5056);
    }

    /* Load objects */
    if (fc_asset_exists("fightcaves.objects")) {
        rs->objects = objects_load("fightcaves.objects");
        if (rs->objects && rs->objects->loaded)
            objects_offset(rs->objects, 2368, 5056);
    }

    /* Load NPC models */
    if (fc_asset_exists("fc_npcs.models"))
        rs->npc_models = fc_npc_models_load("fc_npcs.models");

    /* Load player model */
    if (fc_asset_exists("fc_player.models"))
        rs->player_model = fc_npc_models_load("fc_player.models");

    /* Init prev state */
    rs->prev_player_hp = 0;
    memset(rs->prev_npc_hp, 0, sizeof(rs->prev_npc_hp));

    rs->initialized = 1;
    fprintf(stderr, "[fc_render] Initialized. Terrain:%s Objects:%s Models:%s\n",
            (rs->terrain && rs->terrain->loaded) ? "OK" : "MISSING",
            (rs->objects && rs->objects->loaded) ? "OK" : "MISSING",
            (rs->npc_models && rs->npc_models->loaded) ? "OK" : "MISSING");
}

/* ======================================================================== */
/* Hitsplat spawning from state deltas                                       */
/* ======================================================================== */

static void fcr_spawn_hitsplat(FcrState* rs, float wx, float wy, float wz, int damage) {
    for (int i = 0; i < FCR_MAX_HITSPLATS; i++) {
        if (!rs->hitsplats[i].active) {
            rs->hitsplats[i] = (FcrHitsplat){
                .active = 1, .world_x = wx, .world_y = wy, .world_z = wz,
                .damage = damage, .frames_left = 60
            };
            return;
        }
    }
}

static void fcr_detect_hitsplats(FcrState* rs, FcState* state) {
    /* Player damage */
    if (state->player.damage_taken_this_tick > 0) {
        float px = (float)state->player.x + 0.5f;
        float py = fcr_ground_y(rs, state->player.x, state->player.y) + 2.5f;
        float pz = -((float)state->player.y + 0.5f);
        fcr_spawn_hitsplat(rs, px, py, pz, state->player.damage_taken_this_tick);
    }

    /* NPC damage */
    for (int i = 0; i < FC_MAX_NPCS; i++) {
        FcNpc* n = &state->npcs[i];
        if (!n->active) continue;
        if (n->damage_taken_this_tick > 0) {
            float nx = (float)n->x + (float)n->size * 0.5f;
            float ny = fcr_ground_y(rs, n->x, n->y) + 1.0f + (float)n->size * 0.5f;
            float nz = -((float)n->y + (float)n->size * 0.5f);
            fcr_spawn_hitsplat(rs, nx, ny, nz, n->damage_taken_this_tick);
        }
    }
}

/* ======================================================================== */
/* Input handling (camera, speed, pause)                                      */
/* ======================================================================== */

static void fcr_handle_input(FcrState* rs) {
    /* Quit */
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) {
        CloseWindow();
        return;
    }

    /* Pause */
    if (IsKeyPressed(KEY_SPACE)) rs->paused = !rs->paused;

    /* TPS control */
    if (IsKeyPressed(KEY_UP) && rs->tps < 30) rs->tps++;
    if (IsKeyPressed(KEY_DOWN) && rs->tps > 1) rs->tps--;

    /* Camera presets */
    if (IsKeyPressed(KEY_FOUR)) { rs->cam_yaw=0; rs->cam_pitch=1.35f; rs->cam_dist=120; }
    if (IsKeyPressed(KEY_FIVE)) { rs->cam_yaw=0; rs->cam_pitch=0.6f; rs->cam_dist=50; }

    /* Camera orbit (right-drag) */
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        rs->cam_yaw += d.x * 0.005f;
        rs->cam_pitch -= d.y * 0.005f;
        if (rs->cam_pitch < 0.1f) rs->cam_pitch = 0.1f;
        if (rs->cam_pitch > 1.4f) rs->cam_pitch = 1.4f;
    }

    /* Camera zoom */
    float wh = GetMouseWheelMove();
    if (wh != 0) {
        rs->cam_dist *= (wh > 0) ? (1.0f/1.15f) : 1.15f;
        if (rs->cam_dist < 5) rs->cam_dist = 5;
        if (rs->cam_dist > 300) rs->cam_dist = 300;
    }
}

/* ======================================================================== */
/* 3D scene drawing                                                          */
/* ======================================================================== */

static void fcr_draw_scene(FcrState* rs, FcState* state) {
    /* Camera target: player position */
    float cx = FC_ARENA_WIDTH * 0.5f, cy = -(FC_ARENA_HEIGHT * 0.5f);
    if (rs->entity_count > 0) {
        cx = (float)rs->entities[0].x + 0.5f;
        cy = -((float)rs->entities[0].y + 0.5f);
    }
    rs->camera.target = (Vector3){cx, 0.5f, cy};
    rs->camera.position = (Vector3){
        cx + rs->cam_dist * cosf(rs->cam_pitch) * sinf(rs->cam_yaw),
        rs->cam_dist * sinf(rs->cam_pitch),
        cy + rs->cam_dist * cosf(rs->cam_pitch) * cosf(rs->cam_yaw)
    };

    BeginMode3D(rs->camera);

    /* Terrain + objects */
    if (rs->terrain && rs->terrain->loaded) {
        rlDisableBackfaceCulling();
        DrawModel(rs->terrain->model, (Vector3){0,0,0}, 1.0f, WHITE);
        rlEnableBackfaceCulling();
    }
    if (rs->objects && rs->objects->loaded) {
        rlDisableBackfaceCulling();
        DrawModel(rs->objects->model, (Vector3){0,0,0}, 1.0f, WHITE);
        rlEnableBackfaceCulling();
    }

    /* Entities */
    for (int i = 0; i < rs->entity_count; i++) {
        FcRenderEntity* e = &rs->entities[i];
        float ex = (float)e->x + (float)e->size * 0.5f;
        float ey = -((float)e->y + (float)e->size * 0.5f);
        float gy = fcr_ground_y(rs, e->x, e->y);

        if (e->entity_type == ENTITY_PLAYER) {
            /* Player: model or cylinder fallback */
            NpcModelEntry* pm = (rs->player_model && rs->player_model->count > 0)
                ? &rs->player_model->entries[0] : NULL;
            if (pm && pm->loaded) {
                float face = state->player.facing_angle;
                rlDisableBackfaceCulling();
                DrawModelEx(pm->model, (Vector3){ex,gy,ey},
                            (Vector3){0,1,0}, face, (Vector3){1,1,1}, WHITE);
                rlEnableBackfaceCulling();
            } else {
                DrawCylinder((Vector3){ex, gy, ey}, 0.4f, 0.4f, 2.0f, 8, FCR_COL_PLAYER);
                DrawCylinderWires((Vector3){ex, gy, ey}, 0.4f, 0.4f, 2.0f, 8, WHITE);
            }
        } else if (!e->is_dead) {
            /* NPC: model or colored cube fallback */
            uint32_t mid = fc_npc_type_to_model_id(e->npc_type);
            NpcModelEntry* nme = rs->npc_models ? fc_npc_model_find(rs->npc_models, mid) : NULL;

            if (nme && nme->loaded) {
                float player_ex = (float)rs->entities[0].x + 0.5f;
                float player_ey = -((float)rs->entities[0].y + 0.5f);
                float face = atan2f(player_ex - ex, player_ey - ey) * (180.0f / 3.14159f);
                rlDisableBackfaceCulling();
                DrawModelEx(nme->model, (Vector3){ex,gy,ey},
                            (Vector3){0,1,0}, face, (Vector3){1,1,1}, WHITE);
                rlEnableBackfaceCulling();
            } else {
                float s = (float)e->size * 0.45f;
                float h = 1.0f + (float)e->size * 0.5f;
                Color col = (e->npc_type > 0 && e->npc_type < 9)
                    ? FCR_NPC_COLORS[e->npc_type] : GRAY;
                if (e->died_this_tick) { h *= 0.3f; col.a = 100; }
                DrawCube((Vector3){ex, gy+h*0.5f, ey}, s*2, h, s*2, col);
                DrawCubeWires((Vector3){ex, gy+h*0.5f, ey}, s*2, h, s*2, WHITE);
            }

            /* Blue circle under NPC */
            float cr = (float)e->size * 0.5f;
            if (cr < 0.4f) cr = 0.4f;
            DrawCircle3D((Vector3){ex, gy+0.05f, ey}, cr,
                         (Vector3){1,0,0}, 90.0f, CLITERAL(Color){80,180,255,255});
        }

        /* HP bar above entity */
        if (e->max_hp > 0 && !e->is_dead) {
            float hf = (float)e->current_hp / (float)e->max_hp;
            float bh = 1.0f + (float)e->size * 0.5f + 0.8f;
            float by = gy + bh;
            float bw = (float)e->size * 0.8f;
            if (bw < 0.6f) bw = 0.6f;
            DrawCube((Vector3){ex,by,ey}, bw, 0.08f, 0.08f, FCR_COL_HP_R);
            DrawCube((Vector3){ex-bw*(1.0f-hf)*0.5f,by,ey}, bw*hf, 0.08f, 0.08f, FCR_COL_HP_G);
        }
    }

    /* Attack target highlight */
    int target_idx = state->player.attack_target_idx;
    if (target_idx >= 0 && target_idx < FC_MAX_NPCS && state->npcs[target_idx].active) {
        FcNpc* tn = &state->npcs[target_idx];
        float tx = (float)tn->x + (float)tn->size * 0.5f;
        float ty = -((float)tn->y + (float)tn->size * 0.5f);
        float tr = (float)tn->size * 0.6f;
        DrawCircle3D((Vector3){tx, 0.05f, ty}, tr, (Vector3){1,0,0}, 90.0f, FCR_COL_TARGET);
    }

    EndMode3D();

    /* 2D hitsplats */
    for (int i = 0; i < FCR_MAX_HITSPLATS; i++) {
        FcrHitsplat* h = &rs->hitsplats[i];
        if (!h->active) continue;
        float rise = (float)(60 - h->frames_left) * 0.02f;
        float alpha = (float)h->frames_left / 60.0f;
        Vector3 wp = {h->world_x, h->world_y + rise, h->world_z};
        Vector2 sp = GetWorldToScreen(wp, rs->camera);
        int sx = (int)sp.x, sy = (int)sp.y;
        if (sx < -50 || sx > FCR_WIN_W+50 || sy < -50 || sy > FCR_WIN_H+50) continue;
        int dmg = h->damage / 10;
        Color bg = (h->damage > 0)
            ? (Color){187,0,0,(unsigned char)(alpha*230)}
            : (Color){0,100,200,(unsigned char)(alpha*230)};
        Color tc = (Color){255,255,255,(unsigned char)(alpha*255)};
        DrawCircle(sx, sy, 12, bg);
        char ds[8]; snprintf(ds, sizeof(ds), "%d", dmg);
        int tw = MeasureText(ds, 14);
        DrawText(ds, sx-tw/2, sy-7, 14, tc);
    }

    /* Prayer overhead icon */
    if (rs->entity_count > 0 && rs->entities[0].prayer != PRAYER_NONE) {
        float px = (float)rs->entities[0].x + 0.5f;
        float pz = -((float)rs->entities[0].y + 0.5f);
        float py = fcr_ground_y(rs, rs->entities[0].x, rs->entities[0].y) + 3.0f;
        Vector2 scr = GetWorldToScreen((Vector3){px, py, pz}, rs->camera);
        int ix = (int)scr.x, iy = (int)scr.y;
        const char* txt = "?";
        Color ic = WHITE;
        if (rs->entities[0].prayer == PRAYER_PROTECT_MELEE) { txt = "M"; ic = (Color){200,60,60,255}; }
        else if (rs->entities[0].prayer == PRAYER_PROTECT_RANGE) { txt = "R"; ic = (Color){60,200,60,255}; }
        else if (rs->entities[0].prayer == PRAYER_PROTECT_MAGIC) { txt = "W"; ic = (Color){60,60,200,255}; }
        DrawCircle(ix, iy, 14, (Color){255,255,255,220});
        int itw = MeasureText(txt, 18);
        DrawText(txt, ix-itw/2, iy-9, 18, ic);
    }
}

/* ======================================================================== */
/* Header + side panel                                                       */
/* ======================================================================== */

static void fcr_draw_ui(FcrState* rs, FcState* state) {
    FcPlayer* p = &state->player;
    char b[256];
    int w = FCR_WIN_W;

    /* Header bar */
    DrawRectangle(0, 0, w, FCR_HEADER_H, FCR_COL_HEADER);
    snprintf(b, sizeof(b), "Fight Caves — Wave %d/%d  [POLICY REPLAY]",
             state->current_wave, FC_NUM_WAVES);
    fcr_text_s(b, 10, 4, 16, FCR_COL_TEXT_Y);
    snprintf(b, sizeof(b), "Tick:%d  TPS:%d  %s  NPCs:%d",
             state->tick, rs->tps, rs->paused ? "PAUSED" : "",
             state->npcs_remaining);
    fcr_text_s(b, 10, 22, 10, FCR_COL_TEXT_DIM);

    /* HP / Prayer in header */
    snprintf(b, sizeof(b), "HP %d/%d  Pray %d/%d",
             p->current_hp/10, p->max_hp/10, p->current_prayer/10, p->max_prayer/10);
    int tw = MeasureText(b, 14);
    fcr_text_s(b, w-tw-10, 8, 14, FCR_COL_TEXT_W);

    /* Side panel */
    int px = w - FCR_PANEL_W;
    int py = FCR_HEADER_H;
    int ph = FCR_WIN_H - FCR_HEADER_H;
    DrawRectangle(px, py, FCR_PANEL_W, ph, FCR_COL_PANEL);
    int y = py + 10;

    /* HP bar */
    {
        float hf = (p->max_hp > 0) ? (float)p->current_hp / (float)p->max_hp : 0;
        DrawRectangle(px+10, y, FCR_PANEL_W-20, 18, FCR_COL_HP_R);
        DrawRectangle(px+10, y, (int)((FCR_PANEL_W-20)*hf), 18, FCR_COL_HP_G);
        snprintf(b, sizeof(b), "HP: %d/%d", p->current_hp/10, p->max_hp/10);
        DrawText(b, px+14, y+2, 12, FCR_COL_TEXT_W);
        y += 24;
    }

    /* Prayer bar */
    {
        float pf = (p->max_prayer > 0) ? (float)p->current_prayer / (float)p->max_prayer : 0;
        DrawRectangle(px+10, y, FCR_PANEL_W-20, 18, FCR_COL_PRAY_BG);
        DrawRectangle(px+10, y, (int)((FCR_PANEL_W-20)*pf), 18, FCR_COL_PRAY_B);
        snprintf(b, sizeof(b), "Pray: %d/%d", p->current_prayer/10, p->max_prayer/10);
        DrawText(b, px+14, y+2, 12, FCR_COL_TEXT_W);
        y += 24;
    }

    /* Active prayer */
    {
        const char* pray_txt = "None";
        if (p->prayer == PRAYER_PROTECT_MELEE) pray_txt = "Protect Melee";
        else if (p->prayer == PRAYER_PROTECT_RANGE) pray_txt = "Protect Range";
        else if (p->prayer == PRAYER_PROTECT_MAGIC) pray_txt = "Protect Magic";
        snprintf(b, sizeof(b), "Prayer: %s", pray_txt);
        DrawText(b, px+10, y, 11, FCR_COL_TEXT_W);
        y += 16;
    }

    /* Consumables */
    snprintf(b, sizeof(b), "Sharks: %d  Pots: %d doses",
             p->sharks_remaining, p->prayer_doses_remaining);
    DrawText(b, px+10, y, 11, FCR_COL_TEXT_DIM);
    y += 16;

    /* Timers */
    snprintf(b, sizeof(b), "Atk:%d Food:%d Pot:%d",
             p->attack_timer, p->food_timer, p->potion_timer);
    DrawText(b, px+10, y, 11, FCR_COL_TEXT_DIM);
    y += 20;

    /* Separator */
    DrawLine(px+5, y, px+FCR_PANEL_W-5, y, FCR_COL_TEXT_DIM);
    y += 8;

    /* NPC list */
    DrawText("NPCs Alive:", px+10, y, 12, FCR_COL_TEXT_Y);
    y += 16;
    for (int i = 0; i < FC_MAX_NPCS; i++) {
        if (y > FCR_WIN_H - 20) break;
        FcNpc* n = &state->npcs[i];
        if (!n->active || n->is_dead) continue;
        const char* name = (n->npc_type >= 0 && n->npc_type <= 8)
            ? FCR_NPC_NAMES[n->npc_type] : "???";
        float hf = (n->max_hp > 0) ? (float)n->current_hp / (float)n->max_hp : 0;
        snprintf(b, sizeof(b), "%s  %d/%d", name, n->current_hp/10, n->max_hp/10);

        /* Mini HP bar */
        DrawRectangle(px+10, y, FCR_PANEL_W-20, 14, FCR_COL_HP_R);
        DrawRectangle(px+10, y, (int)((FCR_PANEL_W-20)*hf), 14, FCR_COL_HP_G);
        DrawText(b, px+14, y+1, 10, FCR_COL_TEXT_W);
        y += 18;
    }

    /* Terminal state */
    if (state->terminal != TERMINAL_NONE) {
        const char* msg = "???";
        Color mc = FCR_COL_TEXT_W;
        if (state->terminal == TERMINAL_PLAYER_DEATH) { msg = "DEATH"; mc = (Color){255,60,60,255}; }
        else if (state->terminal == TERMINAL_CAVE_COMPLETE) { msg = "VICTORY!"; mc = (Color){60,255,60,255}; }
        else if (state->terminal == TERMINAL_TICK_CAP) { msg = "TICK CAP"; mc = FCR_COL_TEXT_Y; }
        int mtw = MeasureText(msg, 30);
        DrawText(msg, (w-FCR_PANEL_W)/2 - mtw/2, FCR_WIN_H/2, 30, mc);
    }

    /* Controls help */
    DrawText("Space:Pause Up/Dn:Speed 4/5:Cam Q:Quit", 10, FCR_WIN_H-18, 10, FCR_COL_TEXT_DIM);
}

/* ======================================================================== */
/* Main render entry point — called from c_render() in fight_caves.h         */
/* ======================================================================== */

static void fcr_render_frame(FcrState* rs, FcState* state) {
    if (!rs->initialized) fcr_init(rs);
    if (WindowShouldClose()) return;

    /* Fill render entities from current state */
    fc_fill_render_entities(state, rs->entities, &rs->entity_count);

    /* Detect hitsplats from per-tick flags */
    fcr_detect_hitsplats(rs, state);

    /* Render frames at 60 FPS, blocking until enough time passes for
     * the configured TPS. This gives smooth camera while game advances
     * at the configured tick rate. */
    double tick_time = 1.0 / (double)rs->tps;
    double start = GetTime();

    do {
        fcr_handle_input(rs);
        if (WindowShouldClose()) return;

        /* Update hitsplat lifetimes */
        for (int i = 0; i < FCR_MAX_HITSPLATS; i++) {
            if (rs->hitsplats[i].active) {
                rs->hitsplats[i].frames_left--;
                if (rs->hitsplats[i].frames_left <= 0)
                    rs->hitsplats[i].active = 0;
            }
        }

        BeginDrawing();
        ClearBackground(FCR_COL_BG);
        fcr_draw_scene(rs, state);
        fcr_draw_ui(rs, state);
        EndDrawing();

    } while (!rs->paused && (GetTime() - start < tick_time));

    /* If paused, keep rendering until unpaused (block PufferLib from stepping) */
    while (rs->paused && !WindowShouldClose()) {
        fcr_handle_input(rs);
        BeginDrawing();
        ClearBackground(FCR_COL_BG);
        fcr_draw_scene(rs, state);
        fcr_draw_ui(rs, state);
        EndDrawing();
    }
}

#endif /* FC_RENDER_H */
