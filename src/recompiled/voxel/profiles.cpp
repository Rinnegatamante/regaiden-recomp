#include "profiles.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace revoxel {

static bool number(const std::string& s, int minimum, int maximum, int& result) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(s.c_str(), &end, 0);
    if (errno || end == s.c_str() || end != s.c_str() + s.size() || value < minimum || value > maximum) return false;
    result = static_cast<int>(value);
    return true;
}

static bool shape_name(const std::string& s, Shape& shape) {
    static const char* names[] = {"auto", "flat", "wall", "prop", "rail", "stairs_n", "stairs_s", "roof_x", "roof_y", "platform"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (s == names[i]) { shape = static_cast<Shape>(i); return true; }
    }
    return false;
}

bool Profiles::load(const char* path) {
    if (!path || !*path) { status = "No profile path"; return false; }
    std::ifstream input(path, std::ios::binary);
    if (!input) { status = rules.empty() ? "No profile file; automatic collision geometry" : "Cannot open profile; previous rules retained"; return false; }
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || length > 256 * 1024) { status = "Profile exceeds 256 KiB"; return false; }
    input.seekg(0);
    std::string text(static_cast<size_t>(length), '\0');
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        status = "Cannot read profile"; return false;
    }
    return parse(text);
}

bool Profiles::parse(const std::string& text) {
    if (text.size() > 256 * 1024) { status = "Profile exceeds 256 KiB"; return false; }
    std::vector<ShapeRule> parsed;
    std::istringstream input(text);
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        for (std::string token; fields >> token;) {
            tokens.push_back(std::move(token));
            if (tokens.size() > 8) break;
        }
        if (tokens.empty()) continue;
        ShapeRule rule;
        bool valid = tokens.size() >= 2;
        if (valid) valid = tokens[1] == "*" || number(tokens[1], 0, 255, rule.room);
        if (valid && (tokens[0] == "tile" || tokens[0] == "tile_fill") && tokens.size() == 5) {
            rule.fill = tokens[0] == "tile_fill";
            valid = number(tokens[2], 0, 511, rule.metatile) && shape_name(tokens[3], rule.shape) &&
                number(tokens[4], 0, 64, rule.height);
        } else if (valid && tokens[0] == "rect" && tokens.size() == 8) {
            valid = number(tokens[2], 0, 4095, rule.x) && number(tokens[3], 0, 4095, rule.y) &&
                number(tokens[4], 1, 4096, rule.width) && number(tokens[5], 1, 4096, rule.depth) &&
                shape_name(tokens[6], rule.shape) && number(tokens[7], 0, 64, rule.height) &&
                rule.x + rule.width <= 4096 && rule.y + rule.depth <= 4096;
        } else {
            valid = false;
        }
        if (!valid || parsed.size() >= 1024) {
            status = "Invalid profile at line " + std::to_string(line_number) + "; previous rules retained";
            return false;
        }
        parsed.push_back(rule);
    }
    std::string message = std::to_string(parsed.size()) + " shape overrides loaded";
    rules.swap(parsed);
    status.swap(message);
    ++version;
    return true;
}

int shaped_height(Shape shape, int height, int x, int y, int width, int depth) {
    height = std::clamp(height, 0, 64);
    if (shape == Shape::Flat || !height || width < 1 || depth < 1) return 0;
    x = std::clamp(x, 0, width - 1); y = std::clamp(y, 0, depth - 1);
    if (shape == Shape::StairsN) return std::max(1, height * (depth - y) / depth);
    if (shape == Shape::StairsS) return std::max(1, height * (y + 1) / depth);
    if (shape == Shape::RoofX || shape == Shape::RoofY) {
        const int coordinate = shape == Shape::RoofX ? x : y;
        const int length = shape == Shape::RoofX ? width : depth;
        const int edge_distance = std::min(coordinate + 1, length - coordinate);
        return height / 2 + (height - height / 2) * std::min(length, edge_distance * 2) / length;
    }
    return height;
}

}
