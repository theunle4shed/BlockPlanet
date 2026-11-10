#pragma once

// The brick sky (an experiment, the owner's design, 2026-11-10;
// docs/research/blockplanet-play-backgrounds.md, Brick skybox): a sphere of
// bricks around the camera, drawn in place of the game's play background,
// spinning against the play sphere's turn. All of it is generated here;
// nothing comes from the game. Off unless BLOCKPLANET_BRICK_SKY=1 or the
// Sky test screen turns it on.

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tetrisphere::brick_sky {

struct Settings {
    bool on = false;
    std::string spin = "gentle";      // still, drift, gentle, normal, wild
    bool against = true;              // turn against the sphere (true) or with it
    float fov_degrees = 70.0f;        // the sky's field of view: wider looks further away
    std::string comfort = "frame";    // none, frame, horizon, both: something still for the eye
    std::string colours = "amber";    // violet, teal, amber, rose, grey (amber: no blue, which Story 1:1 uses)
    float brightness = 0.55f;         // 0.3 .. 1
};

Settings settings();
// "sky_on", "sky_spin", "sky_direction", "sky_fov", "sky_comfort",
// "sky_colours", "sky_brightness" as the Sky test screen sends them. Returns
// whether `name` was one of them.
bool set(const std::string& name, const std::string& value);
// The settings as the screen shows them (name, value).
std::vector<std::pair<std::string, std::string>> values();

// How strongly the sky follows the sphere's turn, for a spin setting.
float strength(const std::string& spin);

// The sky's motion. Fed the play sphere's two angles (8192 a turn) each draw;
// keeps the sky's yaw and pitch (radians). Eased (a critically damped
// follow), capped at half a turn a second, with a slow drift of its own
// unless the spin is "still".
struct Motion {
    float yaw = 0, pitch = 0;
    float yaw_speed = 0, pitch_speed = 0;  // radians a second
    bool started = false;
    std::int16_t last_x = 0, last_y = 0;
    void step(std::int16_t sphere_x, std::int16_t sphere_y, float seconds, const Settings& s);
};
constexpr float max_speed = 3.14159265f;  // radians a second: half a turn

// The difference between two of the game's angles, wrapped (16 bits).
inline int angle_step(std::int16_t from, std::int16_t to) { return std::int16_t(std::uint16_t(to) - std::uint16_t(from)); }

struct Vertex { std::int16_t x, y, z; std::uint8_t r, g, b, a; };
// The sky's bricks: six faces of `per_face` x `per_face`, each brick 8
// vertices (its outer corners, dark like mortar, then its face's corners),
// on a sphere of `radius`, seen from the inside.
std::vector<Vertex> bricks(const Settings& s, int per_face = 8, float radius = 1000.0f);
constexpr int vertices_per_brick = 8;
constexpr int triangles_per_brick = 10;

// Guest memory BlockPlanet uses for the sky during play (above 4 MB, past
// the title's own use, which is rebuilt each title frame).
constexpr std::uint32_t memory = 0x807E0000u;
constexpr std::uint32_t memory_end = 0x807F8000u;

// Writes the sky's display list (its own projection and turn, the bricks,
// then the comfort layer) into BlockPlanet's memory and returns its
// address, or 0 if it does not fit. `projection` is the projection to load
// again at the end (0: none). Ends with G_ENDDL; call it with one G_DL.
std::uint32_t build(std::uint8_t* rdram, const Settings& s, const Motion& m, std::uint32_t projection, int frame,
                    float picture_aspect = 4.0f / 3.0f);

// Called at the game's background draw (0x8002D93C with the background
// scene, from the main loop) during play: when the sky is on, adds it to the
// game's display list and returns true (skip the game's background).
bool draw(std::uint8_t* rdram);

// The game picture's shape (width / height, 4:3 or wider when expanded), so
// the comfort layer reaches the picture's edges, not the 4:3 frame's.
void set_picture_aspect(float aspect);

} // namespace tetrisphere::brick_sky
