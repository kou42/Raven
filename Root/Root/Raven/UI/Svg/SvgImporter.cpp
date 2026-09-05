#include "Raven/UI/Svg/SvgImporter.h"

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Animation/AnimationTrack.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raven
{

namespace
{
using AttributeMap = std::unordered_map<std::string, std::string>;

struct SvgScalarAnimation
{
    std::string Attribute;
    float From = 0.0f;
    float To = 0.0f;
    float Duration = 0.0f;
    bool Loop = false;
};

AttributeMap ParseAttributes(const std::string& text)
{
    AttributeMap attributes;
    const std::regex attributeRegex(R"(([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)"))");
    for (std::sregex_iterator it(text.begin(), text.end(), attributeRegex), end; it != end; ++it)
    {
        attributes[(*it)[1].str()] = (*it)[2].str();
    }
    return attributes;
}

bool TryParseFloat(const std::string& text, float& outValue)
{
    try
    {
        std::size_t parsed = 0u;
        outValue = std::stof(text, &parsed);
        return parsed > 0u;
    }
    catch (...)
    {
        return false;
    }
}

bool TryParseSeconds(const std::string& text, float& outSeconds)
{
    if (text.empty())
    {
        return false;
    }

    std::string numeric = text;
    if (numeric.size() >= 2u && numeric.substr(numeric.size() - 2u) == "ms")
    {
        numeric.resize(numeric.size() - 2u);
        float milliseconds = 0.0f;
        if (TryParseFloat(numeric, milliseconds) == false)
        {
            return false;
        }
        outSeconds = milliseconds / 1000.0f;
        return true;
    }

    if (numeric.back() == 's')
    {
        numeric.pop_back();
    }
    return TryParseFloat(numeric, outSeconds);
}

bool TryParseHexByte(const std::string& text, float& outValue)
{
    try
    {
        const int value = std::stoi(text, nullptr, 16);
        outValue = static_cast<float>(value) / 255.0f;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

math::Vec4 ParseColor(const std::string& text)
{
    if (text == "red")
    {
        return math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    if (text == "green")
    {
        return math::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    }
    if (text == "blue")
    {
        return math::Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    if (text == "black")
    {
        return math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (text == "white")
    {
        return math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (text.size() == 7u && text[0] == '#')
    {
        math::Vec4 color(1.0f, 1.0f, 1.0f, 1.0f);
        if (TryParseHexByte(text.substr(1u, 2u), color.x) == true &&
            TryParseHexByte(text.substr(3u, 2u), color.y) == true &&
            TryParseHexByte(text.substr(5u, 2u), color.z) == true)
        {
            return color;
        }
    }
    return math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

float EvaluateScalar(const SvgScalarAnimation* animation, float baseValue, float time)
{
    if (animation == nullptr || animation->Duration <= 0.0f)
    {
        return baseValue;
    }
    const float alpha = std::clamp(time / animation->Duration, 0.0f, 1.0f);
    return animation->From * (1.0f - alpha) + animation->To * alpha;
}

const SvgScalarAnimation* FindAnimation(
    const std::vector<SvgScalarAnimation>& animations,
    const std::string& attribute)
{
    for (const SvgScalarAnimation& animation : animations)
    {
        if (animation.Attribute == attribute)
        {
            return &animation;
        }
    }
    return nullptr;
}

void AppendVec2Track(
    AnimationClip& clip,
    const std::string& targetPath,
    const std::string& property,
    const math::Vec2& baseValue,
    const SvgScalarAnimation* xAnimation,
    const SvgScalarAnimation* yAnimation)
{
    if (xAnimation == nullptr && yAnimation == nullptr)
    {
        return;
    }

    std::vector<float> times{ 0.0f };
    if (xAnimation != nullptr)
    {
        times.push_back(xAnimation->Duration);
    }
    if (yAnimation != nullptr)
    {
        times.push_back(yAnimation->Duration);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    PropertyAnimationTrack<math::Vec2> track;
    track.Binding.TargetPath = targetPath;
    track.Binding.Property = property;
    for (float time : times)
    {
        AnimationKeyframe<math::Vec2> key;
        key.Time = time;
        key.Value.x = EvaluateScalar(xAnimation, baseValue.x, time);
        key.Value.y = EvaluateScalar(yAnimation, baseValue.y, time);
        track.Curve.GetKeys().push_back(key);
    }
    clip.AddPropertyTrack(std::move(track));
}

void AppendOpacityTrack(
    AnimationClip& clip,
    const std::string& targetPath,
    const SvgScalarAnimation* animation)
{
    if (animation == nullptr)
    {
        return;
    }

    PropertyAnimationTrack<float> track;
    track.Binding.TargetPath = targetPath;
    track.Binding.Property = "Opacity";
    track.Curve.GetKeys().push_back(AnimationKeyframe<float>{ 0.0f, animation->From });
    track.Curve.GetKeys().push_back(AnimationKeyframe<float>{ animation->Duration, animation->To });
    clip.AddPropertyTrack(std::move(track));
}

bool ReadTextFile(const std::string& path, std::string& outText)
{
    std::ifstream stream(path, std::ios::binary);
    if (stream.is_open() == false)
    {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    outText = buffer.str();
    return true;
}

} // namespace

bool SvgImporter::ImportFile(const std::string& path, SvgDocument& outDocument, std::string* outError)
{
    std::string source;
    if (ReadTextFile(path, source) == false)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open SVG file: " + path;
        }
        return false;
    }

    SvgDocument document;
    const std::regex svgRegex(R"(<svg\b([^>]*)>)", std::regex::icase);
    std::smatch svgMatch;
    if (std::regex_search(source, svgMatch, svgRegex) == false)
    {
        if (outError != nullptr)
        {
            *outError = "SVG root element was not found.";
        }
        return false;
    }

    const AttributeMap svgAttributes = ParseAttributes(svgMatch[1].str());
    const auto widthIt = svgAttributes.find("width");
    const auto heightIt = svgAttributes.find("height");
    if (widthIt == svgAttributes.end() || heightIt == svgAttributes.end() ||
        TryParseFloat(widthIt->second, document.ViewportSize.x) == false ||
        TryParseFloat(heightIt->second, document.ViewportSize.y) == false)
    {
        if (outError != nullptr)
        {
            *outError = "SVG width/height must be numeric in the initial Raven subset.";
        }
        return false;
    }

    const std::regex rectRegex(R"(<rect\b([^>]*?)(?:/>|>([\s\S]*?)</rect>))", std::regex::icase);
    const std::regex animateRegex(R"(<animate\b([^>]*)/?>)", std::regex::icase);
    std::size_t generatedNameIndex = 0u;
    float maxDuration = 0.0f;

    for (std::sregex_iterator rectIt(source.begin(), source.end(), rectRegex), end; rectIt != end; ++rectIt)
    {
        const AttributeMap attributes = ParseAttributes((*rectIt)[1].str());
        SvgRectElement rectangle;
        auto readNumber = [&attributes](const char* name, float& value)
        {
            const auto it = attributes.find(name);
            if (it == attributes.end())
            {
                return true;
            }
            return TryParseFloat(it->second, value);
        };
        if (readNumber("x", rectangle.Position.x) == false ||
            readNumber("y", rectangle.Position.y) == false ||
            readNumber("width", rectangle.Size.x) == false ||
            readNumber("height", rectangle.Size.y) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG rect contains an unsupported numeric value.";
            }
            return false;
        }

        const auto idIt = attributes.find("id");
        if (idIt != attributes.end())
        {
            rectangle.Name = idIt->second;
        }
        else
        {
            rectangle.Name = "rect" + std::to_string(generatedNameIndex++);
        }

        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            rectangle.FillColor = ParseColor(fillIt->second);
        }

        std::vector<SvgScalarAnimation> animations;
        const std::string body = (*rectIt)[2].str();
        for (std::sregex_iterator animateIt(body.begin(), body.end(), animateRegex); animateIt != end; ++animateIt)
        {
            const AttributeMap animateAttributes = ParseAttributes((*animateIt)[1].str());
            const auto nameIt = animateAttributes.find("attributeName");
            const auto fromIt = animateAttributes.find("from");
            const auto toIt = animateAttributes.find("to");
            const auto durationIt = animateAttributes.find("dur");
            if (nameIt == animateAttributes.end() || fromIt == animateAttributes.end() ||
                toIt == animateAttributes.end() || durationIt == animateAttributes.end())
            {
                continue;
            }

            const std::string& attribute = nameIt->second;
            if (attribute != "x" && attribute != "y" && attribute != "width" &&
                attribute != "height" && attribute != "opacity")
            {
                continue;
            }

            SvgScalarAnimation animation;
            animation.Attribute = attribute;
            if (TryParseFloat(fromIt->second, animation.From) == false ||
                TryParseFloat(toIt->second, animation.To) == false ||
                TryParseSeconds(durationIt->second, animation.Duration) == false ||
                animation.Duration <= 0.0f)
            {
                continue;
            }

            const auto repeatIt = animateAttributes.find("repeatCount");
            animation.Loop = repeatIt != animateAttributes.end() && repeatIt->second == "indefinite";
            document.LoopAnimation = document.LoopAnimation || animation.Loop;
            maxDuration = std::max(maxDuration, animation.Duration);
            animations.push_back(animation);
        }

        document.Rectangles.push_back(rectangle);
        AppendVec2Track(
            document.Animation,
            rectangle.Name,
            "Position",
            rectangle.Position,
            FindAnimation(animations, "x"),
            FindAnimation(animations, "y"));
        AppendVec2Track(
            document.Animation,
            rectangle.Name,
            "Size",
            rectangle.Size,
            FindAnimation(animations, "width"),
            FindAnimation(animations, "height"));
        AppendOpacityTrack(
            document.Animation,
            rectangle.Name,
            FindAnimation(animations, "opacity"));
    }

    document.Animation.SetDuration(maxDuration);
    outDocument = std::move(document);
    return true;
}

} // namespace Raven
