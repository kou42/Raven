#include "Raven/UI/Svg/SvgImporter.h"

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Animation/AnimationTrack.h"

#include <algorithm>
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

    // SVG circleのcx/cy/rをRaven UIのPosition/Sizeへ変換します。
    // r変更は左上Positionも同時に変える必要があるため、同じ時刻列から2本のTrackを生成して整合を保ちます。
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

    // EllipseもUICircleの非正方Sizeとして描画します。
    // rx/ry変更時に中心が動かないよう、PositionとSizeを同じ時刻列から同時生成します。
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
    std::size_t generatedRectIndex = 0u;
    std::size_t generatedCircleIndex = 0u;
    std::size_t generatedEllipseIndex = 0u;
    float maxDuration = 0.0f;
    std::unordered_set<std::string> usedNames;

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

        if (rectangle.Size.x < 0.0f || rectangle.Size.y < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG rect width/height must not be negative.";
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
        ParseScalarAnimations(
            (*rectIt)[2].str(),
            { "x", "y", "width", "height", "opacity" },
            animations,
            document,
            maxDuration);

        const std::size_t elementIndex = document.Rectangles.size();
        document.Rectangles.push_back(rectangle);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Rect,
            elementIndex,
            static_cast<std::size_t>(rectIt->position()) });

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

    for (std::sregex_iterator circleIt(source.begin(), source.end(), circleRegex), end; circleIt != end; ++circleIt)
    {
        const AttributeMap attributes = ParseAttributes((*circleIt)[1].str());
        SvgCircleElement circle;

        const auto cxIt = attributes.find("cx");
        const auto cyIt = attributes.find("cy");
        const auto radiusIt = attributes.find("r");
        if ((cxIt != attributes.end() && TryParseFloat(cxIt->second, circle.Center.x) == false) ||
            (cyIt != attributes.end() && TryParseFloat(cyIt->second, circle.Center.y) == false) ||
            radiusIt == attributes.end() ||
            TryParseFloat(radiusIt->second, circle.Radius) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG circle requires numeric cx/cy and a numeric r.";
            }
            return false;
        }

        if (circle.Radius < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG circle radius must not be negative.";
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
        ParseScalarAnimations(
            (*circleIt)[2].str(),
            { "cx", "cy", "r", "opacity" },
            animations,
            document,
            maxDuration);

        const std::size_t elementIndex = document.Circles.size();
        document.Circles.push_back(circle);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Circle,
            elementIndex,
            static_cast<std::size_t>(circleIt->position()) });

        AppendCircleTracks(document.Animation, circle, animations);
        AppendOpacityTrack(
            document.Animation,
            circle.Name,
            FindAnimation(animations, "opacity"));
    }

    for (std::sregex_iterator ellipseIt(source.begin(), source.end(), ellipseRegex), end; ellipseIt != end; ++ellipseIt)
    {
        const AttributeMap attributes = ParseAttributes((*ellipseIt)[1].str());
        SvgEllipseElement ellipse;

        const auto cxIt = attributes.find("cx");
        const auto cyIt = attributes.find("cy");
        const auto rxIt = attributes.find("rx");
        const auto ryIt = attributes.find("ry");
        if ((cxIt != attributes.end() && TryParseFloat(cxIt->second, ellipse.Center.x) == false) ||
            (cyIt != attributes.end() && TryParseFloat(cyIt->second, ellipse.Center.y) == false) ||
            rxIt == attributes.end() || ryIt == attributes.end() ||
            TryParseFloat(rxIt->second, ellipse.Radius.x) == false ||
            TryParseFloat(ryIt->second, ellipse.Radius.y) == false)
        {
            if (outError != nullptr)
            {
                *outError = "SVG ellipse requires numeric cx/cy and numeric rx/ry.";
            }
            return false;
        }

        if (ellipse.Radius.x < 0.0f || ellipse.Radius.y < 0.0f)
        {
            if (outError != nullptr)
            {
                *outError = "SVG ellipse rx/ry must not be negative.";
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
        ParseScalarAnimations(
            (*ellipseIt)[2].str(),
            { "cx", "cy", "rx", "ry", "opacity" },
            animations,
            document,
            maxDuration);

        const std::size_t elementIndex = document.Ellipses.size();
        document.Ellipses.push_back(ellipse);
        document.Shapes.push_back(SvgShapeReference{
            SvgShapeType::Ellipse,
            elementIndex,
            static_cast<std::size_t>(ellipseIt->position()) });

        AppendEllipseTracks(document.Animation, ellipse, animations);
        AppendOpacityTrack(
            document.Animation,
            ellipse.Name,
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
