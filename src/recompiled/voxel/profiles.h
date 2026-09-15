#ifndef RE_VOXEL_PROFILES_H
#define RE_VOXEL_PROFILES_H

#include <cstdint>
#include <string>
#include <vector>

namespace revoxel {

enum class Shape : uint8_t { Auto, Flat, Wall, Prop, Rail, StairsN, StairsS, RoofX, RoofY, Platform };
struct ShapeRule {
    int room = -1;
    int metatile = -1;
    int x = 0, y = 0, width = 0, depth = 0;
    int height = 0;
    Shape shape = Shape::Auto;
    bool fill = false;
};

class Profiles {
public:
    bool load(const char* path);
    bool parse(const std::string& text);
    const std::vector<ShapeRule>& entries() const { return rules; }
    const std::string& message() const { return status; }
    uint64_t revision() const { return version; }
private:
    std::vector<ShapeRule> rules;
    std::string status = "Automatic collision geometry";
    uint64_t version = 1;
};

int shaped_height(Shape shape, int height, int x, int y, int width, int depth);

}
#endif
