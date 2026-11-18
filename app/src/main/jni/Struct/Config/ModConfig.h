#pragma once

namespace cfg {

namespace esp {
    inline bool  box              = false;
    inline int   box_type         = 1; // 0 = Corner, 1 = Full Box, 2 = Glow
    inline bool  name             = false;
    inline bool  health           = false;
    inline bool  armor            = false;
    inline bool  distance         = false;
    inline bool  weapon           = false;
    inline bool  bot_tag          = false;
    inline bool  ignore_bots      = false;
    inline bool  tracer           = false;
    inline bool  crosshair        = false;
    inline bool  firing_ind       = false;
    inline bool  loot             = false;

    inline float max_range        = 250.0f;
    inline float loot_range       = 150.0f;
    inline float box_rounding     = 3.0f;
    inline float box_thickness    = 2.0f;
    inline float tracer_thickness = 1.2f;
} // namespace esp

namespace aim {
    inline bool  enable           = false;
    inline bool  scope_aim        = true;   // Scope Aim (Instant Head)
    inline bool  sniper_aim       = true;   // Sniper Headshot Lock
    inline bool  show_fov         = false;
    inline float fov_radius       = 180.0f;
    inline float smooth           = 6.0f;
    inline float headshot_rate    = 100.0f; // 0% - 100%
    inline int   target_bone      = 0;      // 0 = Head, 1 = Neck / Chest, 2 = Pelvis / Body (mBody)
    inline int   trigger_mode     = 0;      // 0 = When Firing, 1 = Always In FOV
    inline int   priority_mode    = 0;      // 0 = Crosshair Distance, 1 = Low HP, 2 = 3D Distance
    inline bool  ignore_knocked   = false;
    inline bool  ignore_bots      = false;
    inline bool  visible_check    = false;
    inline float max_distance     = 200.0f;
} // namespace aim

} // namespace cfg
