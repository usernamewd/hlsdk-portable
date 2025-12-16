/***
*
*	Extended ESP (Extra Sensory Perception) implementation for xash3d-fwgs
*	Client-side entity visualization with advanced features
*	Educational use; gated by sv_cheats. Draws 2D boxes, optional LOS, labels, health tint.
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "hud.h"
#include "cl_util.h"
#include "triangleapi.h"
#include "com_model.h"
#include "event_api.h"
#include "pm_defs.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Define ANDROID if building for Android
#if defined(__ANDROID__) || defined(ANDROID)
#define ANDROID 1
#endif

static cvar_t cl_esp          = { "cl_esp",          "0",  FCVAR_ARCHIVE };
static cvar_t cl_esp_rate     = { "cl_esp_rate",     "30", FCVAR_ARCHIVE };
static cvar_t cl_esp_pad      = { "cl_esp_pad",      "6",  FCVAR_ARCHIVE };
static cvar_t cl_esp_width    = { "cl_esp_width",    "2",  FCVAR_ARCHIVE };
static cvar_t cl_esp_alpha    = { "cl_esp_alpha",    "220",FCVAR_ARCHIVE };
static cvar_t cl_esp_los      = { "cl_esp_los",      "0",  FCVAR_ARCHIVE };
static cvar_t cl_esp_labels   = { "cl_esp_labels",   "1",  FCVAR_ARCHIVE };
static cvar_t cl_esp_scientists = { "cl_esp_scientists", "0", FCVAR_ARCHIVE };

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool WorldToScreenPx(const Vector& world, int& sx, int& sy)
{
    float screen[3];
    float world_array[3];
    world.CopyToArray(world_array);
    int behind = gEngfuncs.pTriAPI->WorldToScreen(world_array, screen);
    if (behind) return false; // behind camera
    
    float nx = screen[0];
    float ny = screen[1];
    // float nz = screen[2]; // not needed for 2D projection
    
    sx = (int)((1.0f + nx) * (ScreenWidth / 2.0f));
    sy = (int)((1.0f - ny) * (ScreenHeight / 2.0f));
    return (sx >= 0 && sx < ScreenWidth && sy >= 0 && sy < ScreenHeight);
}

static void DrawRect(int x1, int y1, int x2, int y2, int r, int g, int b, int a, int w, bool fill)
{
    x1 = clampi(x1, 0, ScreenWidth - 1);
    x2 = clampi(x2, 0, ScreenWidth - 1);
    y1 = clampi(y1, 0, ScreenHeight - 1);
    y2 = clampi(y2, 0, ScreenHeight - 1);
    if (x2 - x1 < 2 || y2 - y1 < 2) return;

    // Outline via filled strips for consistent thickness
    gEngfuncs.pfnFillRGBABlend(x1, y1, x2 - x1 + 1, w, r, g, b, a);           // top
    gEngfuncs.pfnFillRGBABlend(x1, y2 - w + 1, x2 - x1 + 1, w, r, g, b, a);   // bottom
    gEngfuncs.pfnFillRGBABlend(x1, y1, w, y2 - y1 + 1, r, g, b, a);           // left
    gEngfuncs.pfnFillRGBABlend(x2 - w + 1, y1, w, y2 - y1 + 1, r, g, b, a);   // right

    if (fill)
    {
        const int fa = a / 6;
        gEngfuncs.pfnFillRGBABlend(x1 + w, y1 + w, (x2 - x1 + 1) - 2 * w, (y2 - y1 + 1) - 2 * w, r, g, b, fa);
    }
}

static bool ComputeScreenBoxFromBBox(const Vector& org, const Vector& mins, const Vector& maxs, int pad, int& x1, int& y1, int& x2, int& y2)
{
    Vector c[8] = {
        org + Vector(mins.x, mins.y, mins.z),
        org + Vector(maxs.x, mins.y, mins.z),
        org + Vector(maxs.x, maxs.y, mins.z),
        org + Vector(mins.x, maxs.y, mins.z),
        org + Vector(mins.x, mins.y, maxs.z),
        org + Vector(maxs.x, mins.y, maxs.z),
        org + Vector(maxs.x, maxs.y, maxs.z),
        org + Vector(mins.x, maxs.y, maxs.z)
    };

    bool any = false;
    int minx =  100000, miny =  100000;
    int maxx = -100000, maxy = -100000;

    for (int i = 0; i < 8; ++i)
    {
        int sx, sy;
        if (!WorldToScreenPx(c[i], sx, sy)) continue;
        any = true;
        if (sx < minx) minx = sx; if (sy < miny) miny = sy;
        if (sx > maxx) maxx = sx; if (sy > maxy) maxy = sy;
    }
    if (!any) return false;

    minx -= pad; miny -= pad; maxx += pad; maxy += pad;
    x1 = minx; y1 = miny; x2 = maxx; y2 = maxy;
    return true;
}

static bool HasLineOfSight(const Vector& src, const Vector& dst)
{
    pmtrace_t tr;
    float src_array[3], dst_array[3];
    src.CopyToArray(src_array);
    dst.CopyToArray(dst_array);
    
    gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(false, true);
    gEngfuncs.pEventAPI->EV_PushPMStates();
    gEngfuncs.pEventAPI->EV_SetSolidPlayers(-1);
    gEngfuncs.pEventAPI->EV_SetTraceHull(2); // standing hull
    gEngfuncs.pEventAPI->EV_PlayerTrace(src_array, dst_array, PM_NORMAL, -1, &tr);
    gEngfuncs.pEventAPI->EV_PopPMStates();
    return tr.fraction >= 1.0f;
}

static bool IsScientistModel(const char* name)
{
    if (!name) return false;
    return strstr(name, "scientist.mdl") != NULL;
}

static void DrawLabel(int x, int y, const char* txt, int r, int g, int b, int a)
{
    // Small shadow then text
    gEngfuncs.pfnDrawSetTextColor(0, 0, 0);
    gEngfuncs.pfnDrawConsoleString(x + 1, y + 1, const_cast<char*>(txt));
    gEngfuncs.pfnDrawSetTextColor(r / 255.0f, g / 255.0f, b / 255.0f);
    gEngfuncs.pfnDrawConsoleString(x, y, const_cast<char*>(txt));
}

class CExtendedESP
{
public:
    CExtendedESP() : m_next(0.0f) {}
    
    void Init()
    {
        gEngfuncs.pfnRegisterVariable(cl_esp.name,          cl_esp.string,          cl_esp.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_rate.name,     cl_esp_rate.string,     cl_esp_rate.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_pad.name,      cl_esp_pad.string,      cl_esp_pad.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_width.name,    cl_esp_width.string,    cl_esp_width.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_alpha.name,    cl_esp_alpha.string,    cl_esp_alpha.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_los.name,      cl_esp_los.string,      cl_esp_los.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_labels.name,   cl_esp_labels.string,   cl_esp_labels.flags);
        gEngfuncs.pfnRegisterVariable(cl_esp_scientists.name, cl_esp_scientists.string, cl_esp_scientists.flags);
        
        #ifdef ANDROID
        gEngfuncs.Con_DPrintf("ESP System initialized for ANDROID - Use 'sv_cheats 1; cl_esp 1' to enable\n");
        #else
        gEngfuncs.Con_DPrintf("ESP System initialized - Use 'sv_cheats 1; cl_esp 1' to enable\n");
        #endif
    }

    void VidInit() {}

    void Redraw(float time, int intermission)
    {
        if (intermission) return;

        // sv_cheats gate: if not set, hard suppress rendering
        const float cheats = gEngfuncs.pfnGetCvarFloat("sv_cheats");
        if (cheats < 1.0f) return;

        if (cl_esp.value < 1.0f) return;

        const float rate = clampf(cl_esp_rate.value, 15.0f, 90.0f);
        const float interval = 1.0f / rate;
        if (time < m_next) return;
        m_next = time + interval;

        const int pad   = clampi((int)cl_esp_pad.value,   0, 24);
        const int width = clampi((int)cl_esp_width.value, 1, 6);
        const int alpha = clampi((int)cl_esp_alpha.value, 30, 255);
        const bool losOnly   = cl_esp_los.value > 0.0f;
        const bool showLabels= cl_esp_labels.value > 0.0f;
        const bool onlySci   = cl_esp_scientists.value > 0.0f;

        cl_entity_t* local = gEngfuncs.GetLocalPlayer();
        if (!local) return;
        
        // Android-specific: Draw a box around the player to test rendering
        #ifdef ANDROID
        if (cl_esp.value >= 1.0f && local)
        {
            int px = (int)((1.0f + 0) * (ScreenWidth / 2.0f));
            int py = (int)((1.0f - 0) * (ScreenHeight / 2.0f));
            int player_box_size = 30;
            
            gEngfuncs.pfnFillRGBABlend(px - player_box_size/2, py - player_box_size/2, player_box_size, 2, 0, 255, 255, 255);  // Top
            gEngfuncs.pfnFillRGBABlend(px - player_box_size/2, py + player_box_size/2 - 2, player_box_size, 2, 0, 255, 255, 255);  // Bottom
            gEngfuncs.pfnFillRGBABlend(px - player_box_size/2, py - player_box_size/2, 2, player_box_size, 0, 255, 255, 255);  // Left
            gEngfuncs.pfnFillRGBABlend(px + player_box_size/2 - 2, py - player_box_size/2, 2, player_box_size, 0, 255, 255, 255);  // Right
        }
        #endif

        // Use player origin with typical eye height offset (17 units up)
        const Vector eye = local->origin + Vector(0, 0, 17);

        int processed_entities = 0;
        int visible_entities = 0;
        
        for (int i = 1; i < 2048; ++i)
        {
            cl_entity_t* ent = gEngfuncs.GetEntityByIndex(i);
            if (!ent) continue;
            
            processed_entities++;
            
            // Android-specific: Be more permissive with entity filtering
            bool hasModel = ent->model != NULL;
            if (hasModel && onlySci && !IsScientistModel(ent->model->name)) continue;

            // Skip local player
            if (ent == local) continue;

            // Android-specific: More lenient origin checking
            bool hasValidOrigin = (ent->origin.x != 0 || ent->origin.y != 0 || ent->origin.z != 0);
            
            #ifdef ANDROID
            // On Android, also check if entity has any health or other properties
            if (!hasValidOrigin) {
                hasValidOrigin = (ent->curstate.health > 0 || ent->curstate.solid != 0 || ent->curstate.movetype != 0);
            }
            #endif
            
            if (!hasValidOrigin) continue;

            // LOS filter
            if (losOnly && !HasLineOfSight(eye, ent->origin))
                continue;

            // BBox preference: use curstate mins/maxs; fallback to human-sized box
            Vector mins = ent->curstate.mins;
            Vector maxs = ent->curstate.maxs;
            if (mins.x == 0 && mins.y == 0 && mins.z == 0 && 
                maxs.x == 0 && maxs.y == 0 && maxs.z == 0)
            {
                mins = Vector(-12, -12, 0);
                maxs = Vector(12, 12, 72);
            }

            int x1, y1, x2, y2;
            if (!ComputeScreenBoxFromBBox(ent->origin, mins, maxs, pad, x1, y1, x2, y2))
                continue;

            visible_entities++;

            // Health tint: red base, add green component if health is high
            const int health = ent->curstate.health; // may be 0 for some ents
            int rr = 255, gg = 0, bb = 0;
            if (health > 0)
            {
                gg = clampi(health, 0, 255) / 2; // soft tint
            }

            DrawRect(x1, y1, x2, y2, rr, gg, bb, alpha, width, true);

            if (showLabels)
            {
                char buf[64];
                const char* name = hasModel && ent->model->name ? ent->model->name : "entity";
                int labelX = x1;
                int labelY = y1 - 10;
                snprintf(buf, sizeof(buf), "%s  hp:%d", name, health);
                DrawLabel(labelX, labelY, buf, 255, 255, 255, 255);
            }
        }
        
        // Debug output every 2 seconds
        static float last_debug_time = 0.0f;
        static bool esp_was_active = false;
        
        if (time - last_debug_time > 2.0f)
        {
            gEngfuncs.Con_DPrintf("ESP Debug: Processed %d entities, %d visible (sv_cheats=%.1f, cl_esp=%.1f)\n", 
                processed_entities, visible_entities, cheats, cl_esp.value);
            last_debug_time = time;
            
            // Android-specific debugging
            #ifdef ANDROID
            gEngfuncs.Con_DPrintf("ANDROID ESP: Screen size %dx%d, local player: %p\n", 
                ScreenWidth, ScreenHeight, local);
            if (local) {
                gEngfuncs.Con_DPrintf("ANDROID ESP: Local origin: %.1f,%.1f,%.1f\n", 
                    local->origin.x, local->origin.y, local->origin.z);
            }
            #endif
        }
        
        // Always draw a test indicator for Android to verify ESP is working
        #ifdef ANDROID
        if (cl_esp.value >= 1.0f)
        {
            // Draw a small green indicator in top-left corner
            int test_x = 10;
            int test_y = 10;
            int test_w = 20;
            int test_h = 20;
            
            gEngfuncs.pfnFillRGBABlend(test_x, test_y, test_w, 2, 0, 255, 0, 255);  // Top
            gEngfuncs.pfnFillRGBABlend(test_x, test_y + test_h - 2, test_w, 2, 0, 255, 0, 255);  // Bottom
            gEngfuncs.pfnFillRGBABlend(test_x, test_y, 2, test_h, 0, 255, 0, 255);  // Left
            gEngfuncs.pfnFillRGBABlend(test_x + test_w - 2, test_y, 2, test_h, 0, 255, 0, 255);  // Right
            
            // Draw status text
            gEngfuncs.pfnDrawSetTextColor(0, 0, 0);
            char status_buf[64];
            snprintf(status_buf, sizeof(status_buf), "ESP:%d", visible_entities);
            gEngfuncs.pfnDrawConsoleString(test_x, test_y + 30, status_buf);
        }
        #endif
        
        // If no entities were visible, draw a test rectangle to verify ESP is working
        if (visible_entities == 0 && cl_esp.value >= 1.0f)
        {
            // Draw a test rectangle in the center of the screen
            int test_x = ScreenWidth / 2 - 50;
            int test_y = ScreenHeight / 2 - 25;
            int test_w = 100;
            int test_h = 50;
            
            gEngfuncs.pfnFillRGBABlend(test_x, test_y, test_w, 2, 255, 0, 0, 255);  // Top
            gEngfuncs.pfnFillRGBABlend(test_x, test_y + test_h - 2, test_w, 2, 255, 0, 0, 255);  // Bottom
            gEngfuncs.pfnFillRGBABlend(test_x, test_y, 2, test_h, 255, 0, 0, 255);  // Left
            gEngfuncs.pfnFillRGBABlend(test_x + test_w - 2, test_y, 2, test_h, 255, 0, 0, 255);  // Right
            
            // Draw test text
            gEngfuncs.pfnDrawSetTextColor(0, 0, 0);
            gEngfuncs.pfnDrawConsoleString(test_x + 5, test_y + 25, const_cast<char*>("ESP TEST"));
        }
    }

private:
    float m_next;
};

// Global instance + hooks
static CExtendedESP gESP;

void ESP_Init()    { gESP.Init(); }
void ESP_VidInit() { gESP.VidInit(); }
void ESP_Redraw(float t, int intermission) { gESP.Redraw(t, intermission); }