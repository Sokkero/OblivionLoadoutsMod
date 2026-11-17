-- Gear Loadouts for The Elder Scrolls IV: Oblivion Remastered (OBSE64 plugin).
-- Build setup adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later.
-- Modified 2026-11-17: new project name, no UI sources, no ESP.

-- no build-machine paths in compiled objects (set BEFORE includes(), see the original project's notes)
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("GearLoadouts")
set_version("0.1.2")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("minhook", "nlohmann_json")

target("GearLoadouts")
    add_rules("commonlibob64.plugin", {
        name = "GearLoadouts",
        author = "CHANGE ME",
        description = "Gear Loadouts: switch between saved sets of worn gear without moving any item (Oblivion Remastered)"
    })
    add_packages("minhook", "nlohmann_json")
    add_syslinks("user32")
    on_load(function (target)
        target:add("defines", "GL_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
    end)
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
