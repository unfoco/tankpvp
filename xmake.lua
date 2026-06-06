add_rules("mode.debug", "mode.release")

add_requires("flecs", {configs = {debug = is_mode("debug")}})
add_requires("box2d", "clay", "enet", "glm", "libsdl3", "libsdl3_image", "libsdl3_ttf")
add_requires("miniaudio", "stb", "glaze")
add_requires("luau", "luabridge3")

target("tankpvp")
    set_kind("binary")
    set_languages("c23", "c++23")
    set_rundir("$(projectdir)")
    add_includedirs("include")
    add_files("src/**.cpp")

    add_packages("box2d", "clay", "enet", "flecs", "glm", "libsdl3", "libsdl3_image", "libsdl3_ttf")
    add_packages("miniaudio", "stb", "glaze", "luau", "luabridge3")

    if is_plat("macosx") then
        add_frameworks("Metal", "QuartzCore", "Cocoa", "AudioToolbox", "CoreAudio", "AudioUnit")
    end
