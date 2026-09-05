#include "Raven/UI/Svg/SvgImporter.h"

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Animation/AnimationTrack.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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

struct LinePose
{
    math::Vec2 Position{};
    math::Vec2 Size{};
    float Rotation = 0.0f;
};

AttributeMap ParseAttributes(const std::string& text)
{
    AttributeMap attributes;
    const std::regex attributeRegex(
        R"REGEX(([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)")REGEX"
    );
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
    if (text == "none")
    {
        return math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
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

bool TryParsePoints(const std::string& text, std::vector<math::Vec2>& outPoints)
{
    outPoints.clear();

    // SVG pointsはcommaとwhitespaceのどちらでも区切れるため、数値tokenを順番に抽出します。
    // exponent表記も許容し、x/yが対にならない入力や3頂点未満はPolygonとして拒否します。
    const std::regex numberRegex(R"([-+]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][-+]?[0-9]+)?)");
    std::vector<float> values;
    for (std::sregex_iterator it(text.begin(), text.end(), numberRegex), end; it != end; ++it)
    {
        float value = 0.0f;
        if (TryParseFloat((*it)[0].str(), value) == false)
        {
            return false;
        }
        values.push_back(value);
    }

    if (values.size() < 6u || (values.size() % 2u) != 0u)
    {
        return false;
    }

    outPoints.reserve(values.size() / 2u);
    for (std::size_t index = 0u; index < values.size(); index += 2u)
    {
        outPoints.push_back(math::Vec2(values[index], values[index + 1u]));
    }
    return true;
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

bool IsSupportedAttribute(
    const std::vector<std::string>& supportedAttributes,
    const std::string& attribute)
{
    return std::find(
        supportedAttributes.begin(),
        supportedAttributes.end(),
        attribute) != supportedAttributes.end();
}

void ParseScalarAnimations(
    const std::string& body,
    const std::vector<std::string>& supportedAttributes,
    std::vector<SvgScalarAnimation>& outAnimations,
    SvgDocument& document,
    float& maxDuration)
{
    const std::regex animateRegex(R"(<animate\b([^>]*)/?>)", std::regex::icase);
    for (std::sregex_iterator animateIt(body.begin(), body.end(), animateRegex), end; animateIt != end; ++animateIt)
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

        if (IsSupportedAttribute(supportedAttributes, nameIt->second) == false)
        {
            continue;
        }

        SvgScalarAnimation animation;
        animation.Attribute = nameIt->second;
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
        outAnimations.push_back(animation);
    }
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

void AppendCircleTracks(
    AnimationClip& clip,
    const SvgCircleElement& circle,
    const std::vector<SvgScalarAnimation>& animations)
{
    const SvgScalarAnimation* cxAnimation = FindAnimation(animations, "cx");
    const SvgScalarAnimation* cyAnimation = FindAnimation(animations, "cy");
    const SvgScalarAnimation* radiusAnimation = FindAnimation(animations, "r");

    if (cxAnimation == nullptr && cyAnimation == nullptr && radiusAnimation == nullptr)
    {
        return;
    }

    std::vector<float> times{ 0.0f };
    if (cxAnimation != nullptr)
    {
        times.push_back(cxAnimation->Duration);
    }
    if (cyAnimation != nullptr)
    {
        times.push_back(cyAnimation->Duration);
    }
    if (radiusAnimation != nullptr)
    {
        times.push_back(radiusAnimation->Duration);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    PropertyAnimationTrack<math::Vec2> positionTrack;
    positionTrack.Binding.TargetPath = circle.Name;
    positionTrack.Binding.Property = "Position";

    PropertyAnimationTrack<math::Vec2> sizeTrack;
    sizeTrack.Binding.TargetPath = circle.Name;
    sizeTrack.Binding.Property = "Size";

    for (float time : times)
    {
        const float centerX = EvaluateScalar(cxAnimation, circle.Center.x, time);
        const float centerY = EvaluateScalar(cyAnimation, circle.Center.y, time);
        const float radius = std::max(0.0f, EvaluateScalar(radiusAnimation, circle.Radius, time));

        positionTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{
            time,
            math::Vec2(centerX - radius, centerY - radius) });
        sizeTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{
            time,
            math::Vec2(radius * 2.0f, radius * 2.0f) });
    }

    clip.AddPropertyTrack(std::move(positionTrack));
    clip.AddPropertyTrack(std::move(sizeTrack));
}

void AppendEllipseTracks(
    AnimationClip& clip,
    const SvgEllipseElement& ellipse,
    const std::vector<SvgScalarAnimation>& animations)
{
    const SvgScalarAnimation* cxAnimation = FindAnimation(animations, "cx");
    const SvgScalarAnimation* cyAnimation = FindAnimation(animations, "cy");
    const SvgScalarAnimation* rxAnimation = FindAnimation(animations, "rx");
    const SvgScalarAnimation* ryAnimation = FindAnimation(animations, "ry");

    if (cxAnimation == nullptr && cyAnimation == nullptr &&
        rxAnimation == nullptr && ryAnimation == nullptr)
    {
        return;
    }

    std::vector<float> times{ 0.0f };
    if (cxAnimation != nullptr)
    {
        times.push_back(cxAnimation->Duration);
    }
    if (cyAnimation != nullptr)
    {
        times.push_back(cyAnimation->Duration);
    }
    if (rxAnimation != nullptr)
    {
        times.push_back(rxAnimation->Duration);
    }
    if (ryAnimation != nullptr)
    {
        times.push_back(ryAnimation->Duration);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    PropertyAnimationTrack<math::Vec2> positionTrack;
    positionTrack.Binding.TargetPath = ellipse.Name;
    positionTrack.Binding.Property = "Position";

    PropertyAnimationTrack<math::Vec2> sizeTrack;
    sizeTrack.Binding.TargetPath = ellipse.Name;
    sizeTrack.Binding.Property = "Size";

    for (float time : times)
    {
        const float centerX = EvaluateScalar(cxAnimation, ellipse.Center.x, time);
        const float centerY = EvaluateScalar(cyAnimation, ellipse.Center.y, time);
        const float radiusX = std::max(0.0f, EvaluateScalar(rxAnimation, ellipse.Radius.x, time));
        const float radiusY = std::max(0.0f, EvaluateScalar(ryAnimation, ellipse.Radius.y, time));

        positionTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{
            time,
            math::Vec2(centerX - radiusX, centerY - radiusY) });
        sizeTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{
            time,
            math::Vec2(radiusX * 2.0f, radiusY * 2.0f) });
    }

    clip.AddPropertyTrack(std::move(positionTrack));
    clip.AddPropertyTrack(std::move(sizeTrack));
}

LinePose CalculateLinePose(
    const math::Vec2& start,
    const math::Vec2& end,
    float strokeWidth)
{
    const float deltaX = end.x - start.x;
    const float deltaY = end.y - start.y;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const math::Vec2 center(
        (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f);

    LinePose pose;
    pose.Size = math::Vec2(length, std::max(0.0f, strokeWidth));
    pose.Position = math::Vec2(
        center.x - pose.Size.x * 0.5f,
        center.y - pose.Size.y * 0.5f);
    pose.Rotation = std::atan2(deltaY, deltaX);
    return pose;
}

void AppendLineTracks(
    AnimationClip& clip,
    const SvgLineElement& line,
    const std::vector<SvgScalarAnimation>& animations)
{
    const SvgScalarAnimation* x1Animation = FindAnimation(animations, "x1");
    const SvgScalarAnimation* y1Animation = FindAnimation(animations, "y1");
    const SvgScalarAnimation* x2Animation = FindAnimation(animations, "x2");
    const SvgScalarAnimation* y2Animation = FindAnimation(animations, "y2");
    const SvgScalarAnimation* widthAnimation = FindAnimation(animations, "stroke-width");

    if (x1Animation == nullptr && y1Animation == nullptr &&
        x2Animation == nullptr && y2Animation == nullptr &&
        widthAnimation == nullptr)
    {
        return;
    }

    std::vector<float> times{ 0.0f };
    const SvgScalarAnimation* allAnimations[] = {
        x1Animation, y1Animation, x2Animation, y2Animation, widthAnimation
    };
    for (const SvgScalarAnimation* animation : allAnimations)
    {
        if (animation != nullptr)
        {
            times.push_back(animation->Duration);
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    PropertyAnimationTrack<math::Vec2> positionTrack;
    positionTrack.Binding.TargetPath = line.Name;
    positionTrack.Binding.Property = "Position";

    PropertyAnimationTrack<math::Vec2> sizeTrack;
    sizeTrack.Binding.TargetPath = line.Name;
    sizeTrack.Binding.Property = "Size";

    PropertyAnimationTrack<float> rotationTrack;
    rotationTrack.Binding.TargetPath = line.Name;
    rotationTrack.Binding.Property = "Rotation";

    for (float time : times)
    {
        const math::Vec2 start(
            EvaluateScalar(x1Animation, line.Start.x, time),
            EvaluateScalar(y1Animation, line.Start.y, time));
        const math::Vec2 end(
            EvaluateScalar(x2Animation, line.End.x, time),
            EvaluateScalar(y2Animation, line.End.y, time));
        const float strokeWidth =
            std::max(0.0f, EvaluateScalar(widthAnimation, line.StrokeWidth, time));
        const LinePose pose = CalculateLinePose(start, end, strokeWidth);

        positionTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{ time, pose.Position });
        sizeTrack.Curve.GetKeys().push_back(AnimationKeyframe<math::Vec2>{ time, pose.Size });
        rotationTrack.Curve.GetKeys().push_back(AnimationKeyframe<float>{ time, pose.Rotation });
    }

    clip.AddPropertyTrack(std::move(positionTrack));
    clip.AddPropertyTrack(std::move(sizeTrack));
    clip.AddPropertyTrack(std::move(rotationTrack));
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

bool RegisterElementName(
    const std::string& name,
    std::unordered_set<std::string>& usedNames,
    std::string* outError)
{
    if (name.empty() || name.find('/') != std::string::npos)
    {
        if (outError != nullptr)
        {
            *outError = "SVG element id is invalid for Raven UI binding: " + name;
        }
        return false;
    }

    if (usedNames.insert(name).second == false)
    {
        if (outError != nullptr)
        {
            *outError = "SVG element id must be unique: " + name;
        }
        return false;
    }
    return true;
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
    const std::regex circleRegex(R"(<circle\b([^>]*?)(?:/>|>([\s\S]*?)</circle>))", std::regex::icase);
    const std::regex ellipseRegex(R"(<ellipse\b([^>]*?)(?:/>|>([\s\S]*?)</ellipse>))", std::regex::icase);
    const std::regex lineRegex(R"(<line\b([^>]*?)(?:/>|>([\s\S]*?)</line>))", std::regex::icase);
    const std::regex polygonRegex(R"(<polygon\b([^>]*?)(?:/>|>([\s\S]*?)</polygon>))", std::regex::icase);

    std::size_t generatedRectIndex = 0u;
    std::size_t generatedCircleIndex = 0u;
    std::size_t generatedEllipseIndex = 0u;
    std::size_t generatedLineIndex = 0u;
    std::size_t generatedPolygonIndex = 0u;
    float maxDuration = 0.0f;
    std::unordered_set<std::string> usedNames;

    for (std::sregex_iterator it(source.begin(), source.end(), rectRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        SvgRectElement rectangle;
        auto readNumber = [&attributes](const char* name, float& value)
        {
            const auto found = attributes.find(name);
            return found == attributes.end() ? true : TryParseFloat(found->second, value);
        };

        if (readNumber("x", rectangle.Position.x) == false ||
            readNumber("y", rectangle.Position.y) == false ||
            readNumber("width", rectangle.Size.x) == false ||
            readNumber("height", rectangle.Size.y) == false ||
            rectangle.Size.x < 0.0f || rectangle.Size.y < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG rect contains an unsupported numeric value.";
            }
            return false;
        }

        const auto idIt = attributes.find("id");
        rectangle.Name = idIt != attributes.end()
            ? idIt->second
            : "rect" + std::to_string(generatedRectIndex++);
        if (RegisterElementName(rectangle.Name, usedNames, outError) == false)
        {
            return false;
        }

        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            rectangle.FillColor = ParseColor(fillIt->second);
        }

        std::vector<SvgScalarAnimation> animations;
        ParseScalarAnimations((*it)[2].str(),
            { "x", "y", "width", "height", "opacity" },
            animations, document, maxDuration);

        const std::size_t elementIndex = document.Rectangles.size();
        document.Rectangles.push_back(rectangle);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Rect, elementIndex, static_cast<std::size_t>(it->position()) });

        AppendVec2Track(document.Animation, rectangle.Name, "Position", rectangle.Position,
            FindAnimation(animations, "x"), FindAnimation(animations, "y"));
        AppendVec2Track(document.Animation, rectangle.Name, "Size", rectangle.Size,
            FindAnimation(animations, "width"), FindAnimation(animations, "height"));
        AppendOpacityTrack(document.Animation, rectangle.Name, FindAnimation(animations, "opacity"));
    }

    for (std::sregex_iterator it(source.begin(), source.end(), circleRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        SvgCircleElement circle;
        const auto cxIt = attributes.find("cx");
        const auto cyIt = attributes.find("cy");
        const auto radiusIt = attributes.find("r");
        if ((cxIt != attributes.end() && TryParseFloat(cxIt->second, circle.Center.x) == false) ||
            (cyIt != attributes.end() && TryParseFloat(cyIt->second, circle.Center.y) == false) ||
            radiusIt == attributes.end() ||
            TryParseFloat(radiusIt->second, circle.Radius) == false ||
            circle.Radius < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG circle requires numeric cx/cy and a non-negative r.";
            }
            return false;
        }

        const auto idIt = attributes.find("id");
        circle.Name = idIt != attributes.end()
            ? idIt->second
            : "circle" + std::to_string(generatedCircleIndex++);
        if (RegisterElementName(circle.Name, usedNames, outError) == false)
        {
            return false;
        }
        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            circle.FillColor = ParseColor(fillIt->second);
        }

        std::vector<SvgScalarAnimation> animations;
        ParseScalarAnimations((*it)[2].str(),
            { "cx", "cy", "r", "opacity" }, animations, document, maxDuration);

        const std::size_t elementIndex = document.Circles.size();
        document.Circles.push_back(circle);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Circle, elementIndex, static_cast<std::size_t>(it->position()) });
        AppendCircleTracks(document.Animation, circle, animations);
        AppendOpacityTrack(document.Animation, circle.Name, FindAnimation(animations, "opacity"));
    }

    for (std::sregex_iterator it(source.begin(), source.end(), ellipseRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        SvgEllipseElement ellipse;
        const auto cxIt = attributes.find("cx");
        const auto cyIt = attributes.find("cy");
        const auto rxIt = attributes.find("rx");
        const auto ryIt = attributes.find("ry");
        if ((cxIt != attributes.end() && TryParseFloat(cxIt->second, ellipse.Center.x) == false) ||
            (cyIt != attributes.end() && TryParseFloat(cyIt->second, ellipse.Center.y) == false) ||
            rxIt == attributes.end() || ryIt == attributes.end() ||
            TryParseFloat(rxIt->second, ellipse.Radius.x) == false ||
            TryParseFloat(ryIt->second, ellipse.Radius.y) == false ||
            ellipse.Radius.x < 0.0f || ellipse.Radius.y < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG ellipse requires numeric cx/cy and non-negative rx/ry.";
            }
            return false;
        }

        const auto idIt = attributes.find("id");
        ellipse.Name = idIt != attributes.end()
            ? idIt->second
            : "ellipse" + std::to_string(generatedEllipseIndex++);
        if (RegisterElementName(ellipse.Name, usedNames, outError) == false)
        {
            return false;
        }
        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            ellipse.FillColor = ParseColor(fillIt->second);
        }

        std::vector<SvgScalarAnimation> animations;
        ParseScalarAnimations((*it)[2].str(),
            { "cx", "cy", "rx", "ry", "opacity" }, animations, document, maxDuration);

        const std::size_t elementIndex = document.Ellipses.size();
        document.Ellipses.push_back(ellipse);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Ellipse, elementIndex, static_cast<std::size_t>(it->position()) });
        AppendEllipseTracks(document.Animation, ellipse, animations);
        AppendOpacityTrack(document.Animation, ellipse.Name, FindAnimation(animations, "opacity"));
    }

    for (std::sregex_iterator it(source.begin(), source.end(), lineRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        SvgLineElement line;
        auto readNumber = [&attributes](const char* name, float& value)
        {
            const auto found = attributes.find(name);
            return found == attributes.end() ? true : TryParseFloat(found->second, value);
        };

        if (readNumber("x1", line.Start.x) == false ||
            readNumber("y1", line.Start.y) == false ||
            readNumber("x2", line.End.x) == false ||
            readNumber("y2", line.End.y) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG line contains an unsupported endpoint value.";
            }
            return false;
        }

        const auto strokeWidthIt = attributes.find("stroke-width");
        if (strokeWidthIt != attributes.end() &&
            (TryParseFloat(strokeWidthIt->second, line.StrokeWidth) == false || line.StrokeWidth < 0.0f))
        {
            if (outError != nullptr)
            {
                *outError = "SVG line stroke-width must be non-negative.";
            }
            return false;
        }

        // SVGのlineはstroke指定がなければ描画されません。透明色を既定値にして仕様へ寄せます。
        line.StrokeColor = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        const auto strokeIt = attributes.find("stroke");
        if (strokeIt != attributes.end())
        {
            line.StrokeColor = ParseColor(strokeIt->second);
        }

        const auto idIt = attributes.find("id");
        line.Name = idIt != attributes.end()
            ? idIt->second
            : "line" + std::to_string(generatedLineIndex++);
        if (RegisterElementName(line.Name, usedNames, outError) == false)
        {
            return false;
        }

        std::vector<SvgScalarAnimation> animations;
        ParseScalarAnimations((*it)[2].str(),
            { "x1", "y1", "x2", "y2", "stroke-width", "opacity" },
            animations, document, maxDuration);

        const std::size_t elementIndex = document.Lines.size();
        document.Lines.push_back(line);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Line, elementIndex, static_cast<std::size_t>(it->position()) });
        AppendLineTracks(document.Animation, line, animations);
        AppendOpacityTrack(document.Animation, line.Name, FindAnimation(animations, "opacity"));
    }

    for (std::sregex_iterator it(source.begin(), source.end(), polygonRegex), end; it != end; ++it)
    {
        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        const auto pointsIt = attributes.find("points");
        SvgPolygonElement polygon;
        if (pointsIt == attributes.end() || TryParsePoints(pointsIt->second, polygon.Points) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG polygon requires at least three valid x/y point pairs.";
            }
            return false;
        }

        const auto idIt = attributes.find("id");
        polygon.Name = idIt != attributes.end()
            ? idIt->second
            : "polygon" + std::to_string(generatedPolygonIndex++);
        if (RegisterElementName(polygon.Name, usedNames, outError) == false)
        {
            return false;
        }
        const auto fillIt = attributes.find("fill");
        if (fillIt != attributes.end())
        {
            polygon.FillColor = ParseColor(fillIt->second);
        }

        std::vector<SvgScalarAnimation> animations;
        // points補間は頂点対応・個数変化の仕様が別途必要なため、初期段階ではOpacityのみ既存Animationへ変換します。
        ParseScalarAnimations((*it)[2].str(),
            { "opacity" }, animations, document, maxDuration);

        const std::size_t elementIndex = document.Polygons.size();
        document.Polygons.push_back(std::move(polygon));
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Polygon, elementIndex, static_cast<std::size_t>(it->position()) });
        AppendOpacityTrack(document.Animation,
            document.Polygons[elementIndex].Name,
            FindAnimation(animations, "opacity"));
    }

    // 型別に解析したshapeをSVGソース中の開始offsetで並べ直します。
    // Retained UI Treeのchild順がそのままDrawList順になるため、ここでXML順を復元しておけば
    // 後に記述されたshapeが前面へ描画されるSVGの基本Painter's Algorithmを維持できます。
    std::sort(
        document.Shapes.begin(),
        document.Shapes.end(),
        [](const SvgShapeReference& left, const SvgShapeReference& right)
        {
            return left.SourceOffset < right.SourceOffset;
        });

    document.Animation.SetDuration(maxDuration);
    outDocument = std::move(document);
    return true;
}

} // namespace Raven
