/***
*
*	ESP (Extra Sensory Perception) implementation for xash3d-fwgs
*	Client-side entity visualization with bounding boxes
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
#include "const.h"
#include "mathlib.h"

static cvar_t* sv_cheats = nullptr;
static cvar_t* esp_enabled = nullptr;
static cvar_t* esp_alpha = nullptr;
static cvar_t* esp_box = nullptr;

// Simple world-to-screen projection function
bool WorldToScreen(const vec3_t worldPos, vec3_t screenPos)
{
    cl_entity_t* player = gEngfuncs.GetLocalPlayer();
    if (!player)
        return false;

    // Get player view angles and origin
    vec3_t playerOrigin = player->origin;
    vec3_t playerAngles = player->angles;

    // Calculate relative position
    vec3_t relativePos;
    VectorSubtract(worldPos, playerOrigin, relativePos);

    // Transform to view space
    vec3_t viewForward, viewRight, viewUp;
    AngleVectors(playerAngles, viewForward, viewRight, viewUp);

    float dotForward = DotProduct(relativePos, viewForward);
    float dotRight = DotProduct(relativePos, viewRight);
    float dotUp = DotProduct(relativePos, viewUp);

    // Check if point is in front of camera
    if (dotForward <= 0)
        return false;

    // Project to screen
    screenPos[0] = (1.0f + (dotRight / dotForward)) * ScreenWidth * 0.5f;
    screenPos[1] = (1.0f - (dotUp / dotForward)) * ScreenHeight * 0.5f;
    screenPos[2] = dotForward; // Store depth for visibility checking

    return true;
}

void ESP_Init()
{
    // Register cvars - ESP should only work when sv_cheats is enabled
    sv_cheats = gEngfuncs.pfnGetCvarPointer("sv_cheats");
    esp_enabled = CVAR_CREATE("esp_enabled", "0", FCVAR_ARCHIVE);
    esp_alpha = CVAR_CREATE("esp_alpha", "180", FCVAR_ARCHIVE);
    esp_box = CVAR_CREATE("esp_box", "1", FCVAR_ARCHIVE);
}

void ESP_Redraw(float time)
{
    // Only render ESP when sv_cheats is enabled
    if (!sv_cheats || sv_cheats->value <= 0.0f)
        return;
    
    // Only render ESP when enabled by cvar
    if (!esp_enabled || esp_enabled->value <= 0.0f)
        return;

    cl_entity_t* player = gEngfuncs.GetLocalPlayer();
    if (!player)
        return;

    vec3_t player_origin = player->origin;
    vec3_t player_angles = player->angles;

    int r = 255, g = 0, b = 0; // Default red color for enemies
    int alpha = (int)esp_alpha->value;

    // Get all entities
    for (int i = 1; i < 1024; i++)
    {
        cl_entity_t* ent = gEngfuncs.GetEntityByIndex(i);
        if (!ent || !ent->model)
            continue;

        // Skip the local player
        if (ent == player)
            continue;

        // Skip entities that are not visible or don't have a model
        if (ent->curstate.messagenum != gEngfuncs.GetMaxClients())
            continue;

        vec3_t ent_origin = ent->origin;
        vec3_t ent_mins = ent->curstate.mins;
        vec3_t ent_maxs = ent->curstate.maxs;

        // Calculate screen position
        vec3_t screen_pos;
        if (!WorldToScreen(ent_origin, screen_pos))
            continue;

        // Calculate bounding box corners
        vec3_t corners[8];
        corners[0][0] = ent_origin[0] + ent_mins[0];
        corners[0][1] = ent_origin[1] + ent_mins[1];
        corners[0][2] = ent_origin[2] + ent_mins[2];
        corners[1][0] = ent_origin[0] + ent_maxs[0];
        corners[1][1] = ent_origin[1] + ent_mins[1];
        corners[1][2] = ent_origin[2] + ent_mins[2];
        corners[2][0] = ent_origin[0] + ent_maxs[0];
        corners[2][1] = ent_origin[1] + ent_maxs[1];
        corners[2][2] = ent_origin[2] + ent_mins[2];
        corners[3][0] = ent_origin[0] + ent_mins[0];
        corners[3][1] = ent_origin[1] + ent_maxs[1];
        corners[3][2] = ent_origin[2] + ent_mins[2];
        corners[4][0] = ent_origin[0] + ent_mins[0];
        corners[4][1] = ent_origin[1] + ent_mins[1];
        corners[4][2] = ent_origin[2] + ent_maxs[2];
        corners[5][0] = ent_origin[0] + ent_maxs[0];
        corners[5][1] = ent_origin[1] + ent_mins[1];
        corners[5][2] = ent_origin[2] + ent_maxs[2];
        corners[6][0] = ent_origin[0] + ent_maxs[0];
        corners[6][1] = ent_origin[1] + ent_maxs[1];
        corners[6][2] = ent_origin[2] + ent_maxs[2];
        corners[7][0] = ent_origin[0] + ent_mins[0];
        corners[7][1] = ent_origin[1] + ent_maxs[1];
        corners[7][2] = ent_origin[2] + ent_maxs[2];

        // Transform corners to screen space
        vec3_t screen_corners[8];
        int visible_corners = 0;
        for (int j = 0; j < 8; j++)
        {
            if (WorldToScreen(corners[j], screen_corners[j]))
            {
                visible_corners++;
            }
        }

        // Only draw if at least some corners are visible
        if (visible_corners >= 4)
        {
            // Calculate min/max screen coordinates
            int min_x = (int)screen_corners[0][0];
            int min_y = (int)screen_corners[0][1];
            int max_x = (int)screen_corners[0][0];
            int max_y = (int)screen_corners[0][1];

            for (int j = 1; j < 8; j++)
            {
                if (screen_corners[j][0] < min_x) min_x = (int)screen_corners[j][0];
                if (screen_corners[j][0] > max_x) max_x = (int)screen_corners[j][0];
                if (screen_corners[j][1] < min_y) min_y = (int)screen_corners[j][1];
                if (screen_corners[j][1] > max_y) max_y = (int)screen_corners[j][1];
            }

            // Draw bounding box if enabled
            if (esp_box->value > 0.0f)
            {
                // Draw the bounding box
                gEngfuncs.pfnFillRGBABlend(min_x, min_y, max_x - min_x, 1, r, g, b, alpha); // Top edge
                gEngfuncs.pfnFillRGBABlend(min_x, max_y, max_x - min_x, 1, r, g, b, alpha); // Bottom edge
                gEngfuncs.pfnFillRGBABlend(min_x, min_y, 1, max_y - min_y, r, g, b, alpha); // Left edge
                gEngfuncs.pfnFillRGBABlend(max_x, min_y, 1, max_y - min_y, r, g, b, alpha); // Right edge
            }

            // Draw entity name above the box
            const char* entity_name = "Player";
            if (ent->player)
            {
                entity_name = g_PlayerInfoList[i].name;
                if (!entity_name || entity_name[0] == '\0')
                    entity_name = "Player";
            }

            // Calculate text position (centered above the box)
            int text_width = gHUD.DrawHudStringLen(entity_name);
            int text_x = (min_x + max_x) / 2 - text_width / 2;
            int text_y = min_y - 15;

            // Draw background for text
            gEngfuncs.pfnFillRGBABlend(text_x - 2, text_y - 2, text_width + 4, 15, 0, 0, 0, 128);
            
            // Draw the text
            gHUD.DrawHudString(text_x, text_y, text_x + text_width + 4, entity_name, 255, 255, 255);
        }
    }
}

void ESP_Think()
{
    // Currently not used, but reserved for future functionality
}