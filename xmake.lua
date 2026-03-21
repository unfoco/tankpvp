add_rules("mode.debug", "mode.release")

add_requires("flecs", {configs = {debug = is_mode("debug")}})
add_requires("box2d", "clay", "enet", "flatbuffers", "glm", "libsdl3", "libsdl3_image", "libsdl3_ttf")

target("tankpvp")
    set_kind("binary")
    set_languages("c23", "c++23")
    set_rundir("$(projectdir)")
    add_includedirs("include")
    add_files("src/**.cpp")

    add_packages("box2d", "clay", "enet", "flatbuffers", "flecs", "glm", "libsdl3", "libsdl3_image", "libsdl3_ttf")

    if is_plat("macosx") then
        add_frameworks("Metal", "QuartzCore", "Cocoa")
    end
