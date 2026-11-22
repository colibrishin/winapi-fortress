# SimMove porting notes

`SimMove.hpp` is a 1:1 port of the client's character walking and falling
code, so the server can run movement and the client, which keeps
predicting with its own code, only needs small corrections. Behaviour was
copied as it is, including code that looks wrong. This file lists where
each piece comes from, where the port has to differ, and which ported
behaviour looks like a bug (to analyse later, in client and sim together).

## Mapping

| Client | Sim |
|---|---|
| `character::update` = `rigidBody::update` + `CharacterController::update` | `step_character` |
| scene update order (characters by player id) | `step_world` |
| `rigidBody::is_collision` | `MoveDetail::is_collision` |
| `character::on_collision` (ground) | `MoveDetail::on_ground_collision` |
| `character::on_collision` (another character) | only sets `collided` (the client does nothing else) |
| `character::on_nocollison` | inline in `step_character` |
| `character::ground_walk` / `ground_gravity` / `ground_pitching` | same names in `MoveDetail` |
| `character::get_next_velocity` / `get_forward_ground` / `search_ground` / `check_angle` | same names in `MoveDetail` |
| `rigidBody::move` + `CharacterController::move` (= `character::move`) | `MoveDetail::move` |
| `rigidBody::apply_gravity` | `MoveDetail::apply_gravity` |
| `rigidBody::is_moving_toward`, `get_velocity_offset`, `get_offset_*_position` | `MoveDetail::is_moving_toward`, `velocity_left`, `offset_*_position` |
| `rigidBody::move_left` / `move_right` / `stop` | `MoveDetail::move_left` / `move_right` / `stop` |
| `CharacterController::idle_state` / `move_state` / `death_state` / `dead_state` | `MoveDetail::controller_update` |
| `CharacterController::default_state` | `MoveDetail::default_state` |
| `BattleScene::movable` / `predicate_OOB` | `MoveDetail::map_movable` / `is_out_of_bounds` |
| `BattleScene::spawnpoints` | `spawn_characters` |
| `Round::apply_turn_change`: `set_unmovable` + `finish_replay` (movement parts) | `finish_turn` |
| `Input::checkKeyState` (Down / Pressing / Up per key) | `MoveDetail::key_state`, `SimCharacter::left_held/right_held` |
| `Ground::safe_is_destroyed`, `safe_is_object_stuck_global`, `safe_nearest_surface`, `safe_orthogonal_surface_global`, `safe_parallel_surface_global`, `object::to_top_left_local_position` | same names on `SimGround` |
| `Math::Vector2` `==`, `normalized`, `magnitude`, `unit_angle`, `local_inner_angle`, `flip_radian_polarity`, `to_degree` | `MoveDetail::same`, `normalized`, ... |
| `rigidBody` / `character` / `CharacterController` members | `SimCharacter`: `velocity`, `gravity_speed`, `gravity`, `grounded`, `mp`, `state`, `facing` (= `m_offset.x`), `pitch` (= movement pitch) |
| character speeds (`Client/*Character.hpp`: cannon/missile {20, 1}, secwind {25, 1}) | `character_move_speed` |

## Intentional deviations

1. **Fixed time step.** The client uses the frame `DeltaTime`; the sim
   uses `dt = tick_seconds` (1/60 s). Walking, mp and the gravity
   integration all scale with dt, and several searches depend on the
   position reached, so a client running at another frame rate drifts
   from the server even with identical inputs.
2. **Crater shape.** Terrain destruction is `SimGround::destroy_circle`
   (pixel centres inside the circle) instead of the client's sin-based
   column fill (this difference already existed for shots). Movement over
   craters depends on the exact pixels, so it can differ near craters.
3. **States.** Only `Idle`, `Move`, `Death` and `Dead` exist. Not modelled:
   - the `TurnEnd` snap in `character::on_collision` (if stuck, move up by
     `safe_nearest_surface`);
   - Firing / Fire / Fired / Hit / Item / PreItem / TurnEnd. Most of them
     call `default_state` too, so a character in them dies from falling the
     same way as in Idle; `fired_state` does **not**, so on the client a
     character in `Fired` cannot die from falling until it leaves that
     state. The server's caller decides which state to report.
4. **Other keys.** `idle_state` checks SPACE, TAB and 1..4 before the A/D
   key-up branch; the sim assumes those are not pressed. On the client,
   pressing one of them in the frame A/D is released skips the `stop()`
   and the character keeps its velocity.
5. **Key edges per character.** The client's `Input` is per machine; the
   sim keeps the last held key per character. The first tick a key is
   held is a key-down edge (`getKey` is false), so walking starts on the
   second tick. If the server's input stream starts while the key is
   already held on the client, the server is one tick late.
6. **Scene objects.** Collisions are checked against the other characters
   (dead ones included, as they stay in the scene) and the grounds, in the
   client's scene order (characters by player id, then grounds in map
   order). Projectiles left in the scene would also count as "collided" on
   the client; the sim ignores them.
7. **Window height.** `apply_gravity` stops falling when the feet are
   within epsilon of `EngineHandle::get_actual_max_y()`; the sim assumes
   the default 600 px client area (`client_window_max_y`).
8. **`m_curr_speed`** is not stored: the acceleration is `{}` for all
   characters, so it always equals `m_speed` while moving.
9. **Remote players.** On the client, other players move from network
   move/stop signals (a stop also overwrites the position). The server
   just steps them with no input and `movable = false`.
10. **Death reporting.** When the state becomes `Death`, `step_character`
    returns true and clears `alive` (the moment the client sends
    `send_own_death`).
11. **Float details.** The sim mirrors the client's float operations
    (`pow(x, 2)`, `atan2`, NaN from normalising a zero or infinite
    vector, epsilon `==`), but bit-exact results are not guaranteed
    across compilers and settings.
12. `spawn_characters` returns early for zero characters (the client
    would divide by zero).

## Quirks ported as-is (look like bugs)

1. **Running out of mp does not stop walking.** `CharacterController::move`
   only stops *consuming* mp below epsilon; nothing reads mp to stop the
   character. mp can end up negative by up to one tick's use (e.g.
   -0.31 after a full 1000 mp walk).
2. **Holding a key prevents the fall death.** `default_state` sets Death,
   then `move_state` sets Idle (because `map->movable` fails) and the held
   key sets Move again. The character falls forever while the key is held
   and dies only on the second tick after release.
3. **80° slope limit only when moving right.** `check_angle` negates the
   angle for a left velocity, so it always passes.
4. **Truncation toward zero** in `safe_is_destroyed`: local coordinates in
   (-1, 0) map to pixel 0. A point up to 1 px above a ground's top edge (or
   left of its left edge) counts as inside. Consequences: feet less than
   1 px deep are already "stuck", and `get_next_velocity` picks an uphill
   direction on flat ground.
5. **Velocity is multiplied component-wise by speed {20, 1}** (`{25, 1}`),
   so a slope direction moves only ~1 px/s vertically and slows the
   horizontal speed (a 1:-2 direction walks at 8.9 px/s). Height changes
   come mostly from gravity and the stuck snap.
6. **`safe_nearest_surface`** lifts by only the fractional part of the
   depth if no destroyed pixel is above the feet in that column (e.g.
   1.5 px deep at the top row: lifted 0.5 px).
7. **Hovering.** A ground is "main" only if the column under the bottom
   centre has an intact pixel. If the boxes overlap but the column has
   none (a hole through the whole ground, or walking past a ground's edge
   while the box still overlaps it), neither `ground_gravity` nor
   `on_nocollison` runs and gravity keeps its last value: the character
   walks or stands on air until its box leaves the ground box.
8. **Overlapping another character** counts as a collision, so
   `on_nocollison` does not turn gravity back on (mid-air hover).
9. **Feet exactly on the surface do not collide** (`bottom - top > epsilon`
   fails), so the character falls one gravity step first; spawned
   characters rest 0.002 px inside the ground.
10. **`on_collision` side check** adds the ground's size to the left local
    position (the `@todo` in the client), so `left_check` is almost always
    OutOfBound.
11. **`ground_pitching` uphill branch** probes forward from the back corner
    but applies the x offset backward.
12. **Pitch is `atan(dy / dx)`**: ±π/2 or NaN when dx is 0 (seen as
    -1.5708 at map edges). Its sign is the screen slope, not "up/down hill"
    relative to the walking direction.
13. **`get_next_velocity` uses the facing**, while `check_angle`,
    `is_moving_toward` and the side check use the velocity direction.
14. **Death line = map height + 500**, not map bottom + 500
    (`BattleScene::m_map_size` is a size): on the desert the bottom is
    y = 150 but the death line is y = 700.
15. **Gravity stops at the window height** (feet within 0.001 of y = 600),
    a leftover of screen coordinates.
16. **Death keeps the velocity**: `death_state` calls `set_unmovable`,
    which only stops in Move state, and the state is already Death.
17. **`set_unmovable` keeps Move state**: it stops, but while Inside a
    ground `ground_walk` immediately sets a new velocity, so the character
    keeps sliding until something sets Idle.
18. **Spawn with more than two players**: the pivot alternates between
    the left and right edge, so the third player goes to
    `x_min - x_max / n` (off the desert; it stays at y = -1000 when no
    ground is below).
19. **Several "main" grounds**: `ground_gravity` and `ground_pitching` run
    for each; the last one in map order wins.
