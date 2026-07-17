# tankpvp

**A server-authoritative multiplayer 2D game engine where the game itself is a Luau mod, not hardcoded rules.**

One binary is the whole stack: client, listen-server, or headless dedicated server. The engine ships zero gameplay. Components, weapons, economies, round logic, HUDs, and win conditions all live in hot-reloadable server-side Luau mods, replicated to clients automatically. The name is a leftover from the prototype; everything below shows **CS-mode**, a complete CT-vs-T bomb defusal gamemode written entirely as a mod, running on the same engine that could just as well run a platformer or a top-down shooter.

![Planting the bomb in CS-mode](asset/image/plant.png)

## The netcode

Built from scratch, tuned until two side-by-side clients match almost 1:1:

- **Server-authoritative simulation** at a fixed 60 Hz. Clients send input intents; the server is the only truth.
- **Client prediction** replays unacknowledged inputs through a box2d shadow world each tick, with error feedback that absorbs corrections smoothly instead of snapping.
- **Micro time dilation** paces the client against the server: exactly one input per tick, cadence adjusted by up to 2% to hold the server's input buffer at target. No stalls, no double-steps, no rubber-banding.
- **Remote players extrapolate to server-present** using replicated velocities, so a shot you see leave someone's barrel leaves at the barrel you are looking at, at the angle you are looking at.
- **Lag-compensated hits**: the server rewinds hitboxes to what the shooter actually saw, with swept collision so fast bullets cannot tunnel through targets.
- **Registry-driven replication**: any component tagged networked serializes automatically from ECS reflection, including components defined in Luau. Deltas are deduplicated per peer, interest-managed by radius, and static entities freeze out of the hot path entirely.
- **Instrumented, not guessed**: run any instance with `--netgraph` for live rtt/jitter/buffer/prediction-error telemetry on the client, tick-gap and input-queue health on the server, and wall-clock frame pacing in the renderer.

## The rest of the engine

- **WebGPU renderer**: 2D shadow-casting lights, line-of-sight vision cones (fog of war), GPU particles, per-sprite materials and custom WGSL shaders, HDR post stack, and fixed-timestep render interpolation for perfectly even motion at any refresh rate.
- **Chunked destructible tile world**: streamed to clients by interest, greedy-meshed box2d collision, per-tile HP with block updates.
- **Three predicted control schemes** selected per entity by a replicated component: differential (tank), top-down (cursor aim), and platformer (gravity and jumps). Prediction never runs scripts; controllers are fixed C++ parameterized by replicated stats.
- **Content-addressed asset pipeline**: mods declare sprites, sounds, and music; clients fetch and cache them by hash on join. No manual asset installs.
- **Server-driven UI**: mods build views (bars, buttons, minimaps, progress popups) that render on specific clients; HUD elements bind directly to replicated components with no per-frame messages.
- **Seamless hot reload**: `world.reload()` swaps the entire mod set on a live server, resyncing the replication registry and connected clients in place.

## What a mod looks like

Real code from CS-mode, lightly trimmed.

**Components are type declarations.** Exporting a type registers the component; `Replicated<>` streams it to clients automatically, `Component<>` stays server-side. Prototypes bundle components into named, spawnable definitions:

```lua
-- data.luau: runs once at load, declares what exists
export type Health = Replicated<{ current: number, max: number }>  -- clients need it for the HUD bar
export type Team = Component<{ side: number }>                     -- server-only state
export type Munition = Component<{ damage: number }>               -- read on EventHit

export type GunProto = Prototype<{ ProjectileWeapon: ProjectileWeapon, Munition: Munition, Ammo: Ammo? }>

GunProto:define("pistol", { ProjectileWeapon = { cooldown = 16, speed = 440, muzzle = 26, life = 3.0 },
                            Munition = { damage = 26 }, Ammo = { mag = 12, reserve = 36, mag_size = 12, reload_time = 1.4 } })
GunProto:define("awp",    { ProjectileWeapon = { cooldown = 78, speed = 820, muzzle = 30, life = 5.0 },
                            Munition = { damage = 120 }, Ammo = { mag = 5, reserve = 20, mag_size = 5, reload_time = 2.8 } })
```

**The engine spawns no player avatar.** The mod declares the body and hands the player control; the engine fills in the input buffer, prediction, firing clock, lag-comp history, ownership, and replication from the components it sees:

```lua
events.on(function(e: EventPlayerJoin)
    local side = (#M.players_on(M.CT) <= #M.players_on(M.T)) and M.CT or M.T
    local sp = M.pick_spawn(side)
    local body = world.spawn{
        Position = { x = sp.x, y = sp.y },
        Rotation = { angle = 0 },
        CollisionBox = { width = 40, height = 30 },
        DifferentialStats = { speed = 160, turn = 3.4 },
        Controller = { scheme = ControlScheme.Differential },
        Dynamic = {},
        HitBox = {},
    }
    e.player:control(body)
    body:sprite({ { tex = "core/tank.png" }, { tex = "core/turret.png", pivot = { 0.25, 0.5 } } })
    body.Health = { current = 100, max = 100 }
    body.Money = { amount = M.START_MONEY }
    M.give(body, "pistol")
end)
```

**Chat commands are declarative.** Argument types come from the handler's annotations; a union of string literals becomes client-side autocomplete, and `§` codes color the reply:

```lua
command.register("team", {
    description = "Switch sides (ct or t)",
    run = function(ctx, side: "ct" | "t")
        local e = ctx.player and ctx.player:entity()
        if not e then return end
        e.Team = { side = (side == "ct") and M.CT or M.T }
        if not e:has(Dying) then e:add(Dying) end   -- sit out until next round
        ctx:reply("§aMoved to " .. side .. " §7(respawn next round)")
    end,
})
```

**Manifests declare identity and dependencies.** Load order is resolved topologically, and a mod can be parked with `enabled` without deleting it:

```json
{ "name": "dust2", "version": "1.0.0", "description": "Bomb defusal map", "depends": ["core"] }
```

## Build & run

```sh
xmake                                          # build
xmake run tankpvp                              # client (menu, then host or connect)
xmake run tankpvp -- --server --port 5000      # dedicated headless server
xmake run tankpvp -- --connect 127.0.0.1:5000  # connect directly
xmake run tankpvp -- --connect 127.0.0.1:5000 --netgraph   # with live net telemetry
```

The engine loads mods from `mods/` at startup, ordered by their manifest dependencies. The CS-mode gamemode and its dust2 map shown above are not part of this repository yet; they will be published as reference mods.

## Status

The engine is feature-complete across its planned phases: netcode (stable and measured), renderer, physics and control schemes, asset pipeline, server-driven UI, graphics settings, touch controls, and the mod manifest system. What remains is content and polish: publishing the reference mods, mod-driven menu branding, and playtesting the touch overlay on real touch hardware. TankPvP itself becomes just another mod.
