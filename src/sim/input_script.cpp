#include "input_script.hpp"
#include <cctype>
#include <fstream>
#include <sstream>

namespace th95::sim {
namespace {

constexpr std::size_t kMaxFrames = 200000;

bool applyToken(th95::platform::InputState &input, const std::string &token)
{
    auto &i = input;
    if (token.empty() || token == "none" || token == "-") return true;
    if (token == "up") { i.up = i.upHeld = true; return true; }
    if (token == "down") { i.down = i.downHeld = true; return true; }
    if (token == "left") { i.left = i.leftHeld = true; return true; }
    if (token == "right") { i.right = i.rightHeld = true; return true; }
    if (token == "upHeld") { i.upHeld = true; return true; }
    if (token == "downHeld") { i.downHeld = true; return true; }
    if (token == "leftHeld") { i.leftHeld = true; return true; }
    if (token == "rightHeld") { i.rightHeld = true; return true; }
    if (token == "shoot") { i.shoot = i.shootHeld = true; return true; }
    if (token == "shootHeld") { i.shootHeld = true; return true; }
    if (token == "focus" || token == "shiftHeld") { i.shiftHeld = true; return true; }
    if (token == "pause") { i.pause = true; return true; }
    if (token == "confirm") { i.confirm = true; return true; }
    if (token == "cancel") { i.cancel = true; return true; }
    if (token == "showRate") { i.showRate = true; return true; }
    if (token == "lockBest") { i.lockBest = true; return true; }
    return false;
}

} // namespace

bool loadInputScript(const std::string &path,
                     std::vector<th95::platform::InputState> &frames,
                     std::string &error)
{
    frames.clear();
    std::ifstream file(path);
    if (!file) {
        error = "cannot open input script " + path;
        return false;
    }
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream stream(line);
        long count = 0;
        if (!(stream >> count)) continue;
        if (count < 0 || count > static_cast<long>(kMaxFrames)) {
            error = "input script line " + std::to_string(lineNumber) + ": count out of range";
            return false;
        }
        std::string tokens;
        if (!(stream >> tokens)) tokens = "none";
        th95::platform::InputState input;
        std::size_t begin = 0;
        while (begin <= tokens.size()) {
            const auto comma = tokens.find(',', begin);
            const auto piece = tokens.substr(begin, comma == std::string::npos
                                                        ? std::string::npos : comma - begin);
            if (!applyToken(input, piece)) {
                error = "input script line " + std::to_string(lineNumber) +
                        ": unknown token '" + piece + "'";
                return false;
            }
            if (comma == std::string::npos) break;
            begin = comma + 1;
        }
        if (frames.size() + static_cast<std::size_t>(count) > kMaxFrames) {
            error = "input script exceeds " + std::to_string(kMaxFrames) + " frames";
            return false;
        }
        frames.insert(frames.end(), static_cast<std::size_t>(count), input);
    }
    if (frames.empty()) {
        error = "input script produced no frames";
        return false;
    }
    return true;
}

} // namespace th95::sim
