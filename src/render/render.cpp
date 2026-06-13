
#include "render.h"

#include "component/network.h"

namespace {
struct Rendering {};
}

Render::Render(flecs::world& world) {
    world.component<RenderState>().add(flecs::Singleton);
    world.component<EmitterState>();

    world.system("render::init").kind(flecs::OnStart).each(Render::init);

    auto begin = world.entity("render::begin_phase").add<Rendering>();
    auto camera = world.entity("render::camera_phase").add<Rendering>().depends_on(begin);
    auto collect = world.entity("render::collect_phase").add<Rendering>().depends_on(camera);
    auto occluder = world.entity("render::occluder_phase").add<Rendering>().depends_on(collect);
    auto compute = world.entity("render::compute_phase").add<Rendering>().depends_on(occluder);
    auto world_pass = world.entity("render::world_phase").add<Rendering>().depends_on(compute);
    auto distortion = world.entity("render::distortion_phase").add<Rendering>().depends_on(world_pass);
    auto light = world.entity("render::light_phase").add<Rendering>().depends_on(distortion);
    auto composite = world.entity("render::composite_phase").add<Rendering>().depends_on(light);
    auto post = world.entity("render::post_phase").add<Rendering>().depends_on(composite);
    auto minimap = world.entity("render::minimap_phase").add<Rendering>().depends_on(post);
    auto ui = world.entity("render::ui_phase").add<Rendering>().depends_on(minimap);
    auto transition = world.entity("render::transition_phase").add<Rendering>().depends_on(ui);
    auto present = world.entity("render::present_phase").add<Rendering>().depends_on(transition);

    world.system("render::begin").kind(begin).immediate().run(Render::begin);
    world.system<RenderState, const Position>("render::camera").kind(camera).with<Local>().each(Render::camera);
    world.system("render::collect").kind(collect).immediate().run(Render::collect);
    world.system("render::occluder").kind(occluder).immediate().run(Render::pass_occluder);
    world.system("render::compute").kind(compute).immediate().run(Render::compute);
    world.system("render::world").kind(world_pass).immediate().run(Render::pass_world);
    world.system("render::distortion").kind(distortion).immediate().run(Render::pass_distortion);
    world.system("render::light").kind(light).immediate().run(Render::pass_light);
    world.system("render::composite").kind(composite).immediate().run(Render::pass_composite);
    world.system("render::post").kind(post).immediate().run(Render::pass_post);
    world.system("render::minimap").kind(minimap).immediate().run(Render::pass_minimap);
    world.system<RenderState, InterfaceCommands>("render::ui").kind(ui).each(Render::interface);
    world.system("render::transition").kind(transition).immediate().run(Render::pass_transition);
    world.system("render::present").kind(present).immediate().run(Render::present);

    world.observer<const RequestParticles>("render::emit").event(flecs::OnSet).each(Render::emit);
    world.observer<const RequestTransition>("render::transition_request").event(flecs::OnSet).each(Render::transition);

    world.observer<const ParticleEmitter>("render::emitter_state").event(flecs::OnSet).each([](flecs::entity e, const ParticleEmitter& em) -> void {
        auto* state = e.try_get_mut<EmitterState>();
        if (state == nullptr) {
            e.set<EmitterState>({.accumulator = 0.0F, .pending_burst = em.burst});
        } else {
            state->pending_burst = static_cast<uint16_t>(std::min<uint32_t>(state->pending_burst + em.burst, 0xFFFF));
        }
    });

    flecs::entity pipeline = world.pipeline().with(flecs::System).with<Rendering>().cascade(flecs::DependsOn).build();
    world.set<RenderPipeline>({pipeline.id()});
}
