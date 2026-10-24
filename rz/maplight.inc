// generated from the level files (Sunlight actors): direction of the main light for the character
// shadows, brightness of characters and props lit by the engine, strength of the shadows
struct MapLight { const char* map; bool sun; float pitch, yaw; float chars, shadows; };
static const MapLight MAP_LIGHT[] = {
    { "10_castle_siege", true, -42.5f, -71.0f, 1.0f, 1.0f },   // brightness 200, hue 170
    { "11_fgm_battle", true, -37.4f, 111.5f, 1.0f, 1.0f },   // brightness 157, hue 143
    { "1_shreks_swamp", true, -28.3f, 0.0f, 1.0f, 1.0f },   // brightness 250, hue 32
    { "3_the_hunt_part1", true, -51.0f, 38.5f, 0.8f, 1.0f },   // brightness 30, hue 170
    { "3_the_hunt_part2", true, -31.8f, -32.8f, 0.8f, 0.75f },   // brightness 30, hue 200
    { "3_the_hunt_part3", true, -31.8f, -32.8f, 0.8f, 0.75f },   // brightness 38, hue 43
    { "3_the_hunt_part4", true, -31.8f, -32.8f, 0.8f, 0.75f },   // brightness 38, hue 42
    { "6_hamlet", true, -36.0f, 180.0f, 1.0f, 1.0f },   // brightness 100, hue 150
    { "6_hamlet_end", true, -18.9f, 84.1f, 1.0f, 1.0f },   // brightness 58, hue 118
    { "beanstalk_bonus", true, -84.3f, -115.3f, 1.0f, 1.0f },   // brightness 64, hue 0
    { "beanstalk_bonus_dawn", false, 0.f, 0.f, 0.78f, 0.8f },   // rain storm by day: characters a little darker
    { "beanstalk_bonus_knight", true, -48.f, -115.f, 0.72f, 0.7f },
};
