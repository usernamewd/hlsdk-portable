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

static cvar_t* sv_cheats = nullptr;
static cvar_t* esp_enabled = nullptr;
static cvar_t* esp_alpha = nullptr;
static cvar_t* esp_box = nullptr;

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

    Vector player_origin = player->origin;
    Vector player_angles = player->angles;

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

        Vector ent_origin = ent->origin;
        Vector ent_mins = ent->mins;
        Vector ent_maxs = ent->maxs;

        // Calculate screen position
        Vector screen_pos;
        if (!WorldToScreen(ent_origin, screen_pos))
            continue;

        // Calculate bounding box corners
        Vector corners[8];
        corners[0] = ent_origin + Vector(ent_mins.x, ent_mins.y, ent_mins.z);
        corners[1] = ent_origin + Vector(ent_maxs.x, ent_mins.y, ent_mins.z);
        corners[2] = ent_origin + Vector(ent_maxs.x, ent_maxs.y, ent_mins.z);
        corners[3] = ent_origin + Vector(ent_mins.x, ent_maxs.y, ent_mins.z);
        corners[4] = ent_origin + Vector(ent_mins.x, ent_mins.y, ent_maxs.z);
        corners[5] = ent_origin + Vector(ent_maxs.x, ent_mins.y, ent_maxs.z);
        corners[6] = ent_origin + Vector(ent_maxs.x, ent_maxs.y, ent_maxs.z);
        corners[7] = ent_origin + Vector(ent_mins.x, ent_maxs.y, ent_maxs.z);

        // Transform corners to screen space
        Vector screen_corners[8];
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
            // Draw bounding box if enabled
            if (esp_box->value > 0.0f)
            {
                // Calculate min/max screen coordinates
                int min_x = (int)screen_corners[0].x;
                int min_y = (int)screen_corners[0].y;
                int max_x = (int)screen_corners[0].x;
                int max_y = (int)screen_corners[0].y;

                for (int j = 1; j < 8; j++)
                {
                    if (screen_corners[j].x < min_x) min_x = (int)screen_corners[j].x;
                    if (screen_corners[j].x > max_x) max_x = (int)screen_corners[j].x;
                    if (screen_corners[j].y < min_y) min_y = (int)screen_corners[j].y;
                    if (screen_corners[j].y > max_y) max_y = (int)screen_corners[j].y;
                }

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