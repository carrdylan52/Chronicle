#include "options/config_game.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

#include "platform/config_store.hpp"
#include "platform/paths.hpp"

namespace {

ConfigStoreOf<Config> g_store(ConfigParse, ConfigSerialize);

constexpr const char *kLanguageNames[] = {"ask", "", "english", "francais", "deutsch", "italiano", "espanol"};

// "ask" or a language's name, as config.json spells them, becoming LanguageCode 0 or 2 to 6.
bool ReadLanguage(const ConfigJson &value, int &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string language = ConfigLower(value.get<std::string>());
    for (std::size_t i = 0; i < std::size(kLanguageNames); ++i) {
        if (kLanguageNames[i][0] != '\0' && language == kLanguageNames[i]) {
            out = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

// The game's own options, each in the section of the Options screen's page that shows it.
bool ApplyGameOption(ConfigGameOptions &options, std::string_view name, const ConfigJson &value) {
    if (name == "game.save_cursor_position") {
        return ConfigReadBool(value, options.save_cursor_position);
    }
    if (name == "input.vibration") {
        return ConfigReadBool(value, options.vibration);
    }
    if (name == "game.message_speed") {
        return ConfigReadChoice(value, "normal", "fast", options.fast_messages);
    }
    if (name == "audio.sound") {
        bool mono = false;
        if (!ConfigReadChoice(value, "stereo", "mono", mono)) {
            return false;
        }
        options.stereo = !mono;
        return true;
    }
    if (name == "game.clock") {
        return ConfigReadBool(value, options.clock);
    }
    if (name == "game.time_speed") {
        return ConfigReadChoice(value, "normal", "fast", options.fast_time);
    }
    if (name == "game.map") {
        if (!value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > 3) {
            return false;
        }
        options.map = value.get<int>();
        return true;
    }
    if (name == "game.enemy_damage") {
        return ConfigReadBool(value, options.enemy_damage);
    }
    if (name == "game.player_damage") {
        return ConfigReadBool(value, options.player_damage);
    }
    if (name == "game.enemy_hp") {
        return ConfigReadBool(value, options.enemy_hp);
    }
    if (name == "game.names") {
        return ConfigReadBool(value, options.names);
    }
    if (name == "video.soft_focus") {
        return ConfigReadBool(value, options.soft_focus);
    }
    return false;
}

bool ReadDistance(const ConfigJson &value, float &out) {
    float distance = 0.0f;
    if (!ConfigReadFloat(value, distance) || !(distance >= 0.0f) || !std::isfinite(distance)) {
        return false;
    }
    out = distance;
    return true;
}

bool Apply(Config &config, std::string_view name, const ConfigJson &value) {
    if (name == "audio.sound" && value == "surround") {
        config.options.stereo = true;
        config.surround = true;
        return true;
    }
    if (ApplyGameOption(config.options, name, value)) {
        return true;
    }
    if (name == "video.text_font") {
        if (!value.is_string() || (value.get<std::string>() != "sharp" && value.get<std::string>() != "original")) {
            return false;
        }
        config.font_sharp = value.get<std::string>() == "sharp";
        return true;
    }
    if (name == "input.mouse_camera_return") {
        float rate = 0.0f;
        if (!ConfigReadFloat(value, rate) || !std::isfinite(rate) || rate < 0.0f || rate > 1.0f) {
            return false;
        }
        config.mouse_camera_return = rate;
        return true;
    }
    if (name == "game.qte_always_win") {
        return ConfigReadBool(value, config.qte_always_win);
    }
    if (name == "game.element_quick_select") {
        return ConfigReadBool(value, config.element_quick_select);
    }
    if (name == "game.debug_mode") {
        return ConfigReadBool(value, config.debug_mode);
    }
    if (name == "video.text_shadow" || name == "video.glyph_shadow" || name == "video.name_shadow" ||
        name == "video.floor_shadow" || name == "video.boss_shadow") {
        int &target = name == "video.text_shadow"    ? config.text_shadow
                      : name == "video.glyph_shadow" ? config.glyph_shadow
                      : name == "video.name_shadow"  ? config.name_shadow
                      : name == "video.floor_shadow" ? config.floor_shadow
                                                     : config.boss_shadow;
        if (value.is_string()) {
            // the earlier two-choice setting
            const std::string shadow = ConfigLower(value.get<std::string>());
            if (shadow != "soft" && shadow != "deep") {
                return false;
            }
            target = shadow == "deep" ? 100 : 50;
            return true;
        }
        double percent = 0.0;
        if (!ConfigReadDouble(value, percent) || !std::isfinite(percent)) {
            return false;
        }
        target = std::clamp(static_cast<int>(std::lround(percent / 5.0)) * 5, 0, 100);
        return true;
    }
    if (name == "game.language") {
        return ReadLanguage(value, config.language);
    }
    if (name == "video.detail_distance") {
        return ReadDistance(value, config.detail_distance);
    }
    if (name == "video.shadow_distance") {
        return ReadDistance(value, config.shadow_distance);
    }
    if (name == "audio.soundtrack") {
        return ConfigReadChoice(value, "ps2", "custom", config.soundtrack);
    }
    return ConfigBaseApply(config, name, value);
}

} // namespace

const Config &ConfigGet() {
    return g_store.Get();
}

float ConfigDetailDistance() {
    float distance = ConfigGet().detail_distance;
    return distance > 0.0f ? distance : std::numeric_limits<float>::infinity();
}

float ConfigShadowDistance() {
    float distance = ConfigGet().shadow_distance;
    return distance > 0.0f ? distance : std::numeric_limits<float>::infinity();
}

Config ConfigParse(std::string_view text) {
    Config config;
    ConfigParseKeys(text, [&](std::string_view name, const ConfigJson &value) { return Apply(config, name, value); });
    return config;
}

std::string ConfigSerialize(const Config &config) {
    ConfigJson root;
    ConfigBaseSerialize(config, root);
    root["game"]["debug_mode"] = config.debug_mode;
    root["game"]["qte_always_win"] = config.qte_always_win;
    root["game"]["element_quick_select"] = config.element_quick_select;
    root["game"]["language"] = kLanguageNames[config.language >= 2 && config.language <= 6 ? config.language : 0];
    const ConfigGameOptions &options = config.options;
    root["game"]["save_cursor_position"] = options.save_cursor_position;
    root["game"]["message_speed"] = options.fast_messages ? "fast" : "normal";
    root["game"]["clock"] = options.clock;
    root["game"]["time_speed"] = options.fast_time ? "fast" : "normal";
    root["game"]["map"] = options.map;
    root["game"]["enemy_damage"] = options.enemy_damage;
    root["game"]["player_damage"] = options.player_damage;
    root["game"]["enemy_hp"] = options.enemy_hp;
    root["game"]["names"] = options.names;
    root["video"]["text_font"] = config.font_sharp ? "sharp" : "original";
    root["video"]["text_shadow"] = config.text_shadow;
    root["video"]["glyph_shadow"] = config.glyph_shadow;
    root["video"]["name_shadow"] = config.name_shadow;
    root["video"]["floor_shadow"] = config.floor_shadow;
    root["video"]["boss_shadow"] = config.boss_shadow;
    root["video"]["detail_distance"] = ConfigShortest(config.detail_distance);
    root["video"]["shadow_distance"] = ConfigShortest(config.shadow_distance);
    root["video"]["soft_focus"] = options.soft_focus;
    if (!config.surround && !options.stereo) {
        root["audio"]["sound"] = "mono";
    }
    root["audio"]["soundtrack"] = config.soundtrack ? "custom" : "ps2";
    root["input"]["mouse_camera_return"] = ConfigShortest(config.mouse_camera_return);
    root["input"]["vibration"] = options.vibration;
    return root.dump(4) + "\n";
}

bool ConfigSave() {
    return g_store.Save();
}

bool ConfigLoad() {
    std::error_code error;
    if (!std::filesystem::is_regular_file(PathsSaveRoot() / "config.json", error) &&
        std::filesystem::exists(PathsSaveRoot() / "config.ini", error)) {
        std::fprintf(stderr, "config.ini is no longer read: move its settings to config.json (docs/PC.md)\n");
    }
    return g_store.Load();
}

void ConfigAddChangeHook(ConfigChangeHook hook) {
    g_store.AddHook(hook);
}

void ConfigAddChangeHook(ConfigHostChangeHook hook) {
    g_store.AddHook(hook);
}

void ConfigRemoveChangeHook(ConfigChangeHook hook) {
    g_store.RemoveHook(hook);
}

void ConfigRemoveChangeHook(ConfigHostChangeHook hook) {
    g_store.RemoveHook(hook);
}

bool ConfigChange(const Config &config) {
    return g_store.Change(config);
}

bool ConfigAppliesOnRestart(std::string_view key) {
    return key == "game.debug_mode";
}
