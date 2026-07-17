# tankpvp

**A server-authoritative multiplayer 2D game engine — games are Luau mods, not hardcoded rules.**

One binary runs as a client, a listen-server, or a headless dedicated server. Everything gameplay-side — components, weapons, rounds, UI, the whole game loop — lives in hot-reloadable Luau mods. The name is a leftover; the screenshots below are **CS-mode**, a full CT-vs-T bomb/defuse gamemode written entirely as a mod, shown here to prove the engine is game-agnostic.

![tankpvp running CS-mode](docs/hero.png)

## What it actually is

- **Server-authoritative netcode** — client-side prediction reconciled against a box2d shadow world, lag-compensated hits, delta-encoded snapshots with per-peer interest management.
- **Registry-driven replication** — any component tagged replicated is serialized automatically from ECS reflection. Components *defined in Luau* replicate to clients the same way engine ones do.
- **Luau modding platform** — define components, typed chat commands with autocomplete, event handlers, server-driven UI, prototypes, scenes, and timers. `world.reload()` hot-swaps mods live.
- **WebGPU renderer** — 2D lighting and shadow casting, vision cones (fog of war), GPU particles, materials/custom shaders, and a post-processing stack.
- **Chunked destructible tile world** — streamed to clients by interest, greedy-meshed collision, tile HP and block updates.

## Gallery

<!-- drop real captures into docs/ -->
| | |
|---|---|
| ![Fog of war](docs/vision.png)<br>**Line-of-sight vision** — cone/radial fog rendered by the engine lighting pass, casting shadows off tiles and occluders. | ![Buy menu](docs/buy.png)<br>**Buy menu** — a server-driven Luau `view` sent to one player; the economy is pure mod state. |
| ![HUD](docs/hud.png)<br>**Component-bound HUD** — the health bar binds directly to the replicated `Health` component, no per-frame messages. | ![Round / bomb](docs/round.png)<br>**Rounds & bomb** — timers, plant/defuse progress popups, and win logic, all driven from the mod's tick handler. |

## A taste of the mod API

The engine spawns no player avatar. The mod declares the game-facing shape and hands the player control — the engine fills in input, physics, the firing clock, lag-comp history, ownership, and replication automatically:

```lua
events.on(function(e: EventPlayerJoin)
    local body = world.spawn{
        Position = { x = 0, y = 0 },
        CollisionBox = { width = 40, height = 30 },
        Controller = { scheme = ControlScheme.Differential },
        Dynamic = {},
        HitBox = {},
    }
    e.player:control(body)
    body.Health = { current = 100, max = 100 }
    body:sprite("core/ct.png")
    M.give(body, "pistol")

    -- a server-driven HUD; the bar binds to the live component
    e.player:open_view("hud", view.column{
        placement = ViewPlacement.TopRight,
        view.bar{ value = Health.current, max = Health.max },
    })
end)
```

Weapons are just data — an engine `ProjectileWeapon` for how it fires, plus mod components for damage and mag:

```lua
GunProto:define("awp", {
    ProjectileWeapon = { cooldown = 78, speed = 820, muzzle = 30, life = 5.0 },
    Munition = { damage = 120 },
    Ammo = { mag = 5, reserve = 20, mag_size = 5, reload_time = 2.8 },
})
```

## Build & run

```sh
xmake                      # build
xmake run tankpvp          # client (menu → host or connect)
xmake run tankpvp -- --server --port 5000    # dedicated headless server
xmake run tankpvp -- --connect 127.0.0.1:5000
```

Mods live in `mods/`. `mods/core` is CS-mode; `mods/dust2` is a map for it.

## Status

Early and evolving. Rough edges remain — next up: AI players and general cleanup.
