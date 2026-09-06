#include "Raven/UI/Svg/SvgPathStyleImporter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Raven
{

namespace
{
using AttributeMap = std::unordered_map<std::string, std::string>;

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

AttributeMap ParseAttributes(const std::string& text)
{
    AttributeMap attributes;
    const std::regex attributeRegex(
        R"REGEX(([A-Za-z_:][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)")REGEX");
    for (std::sregex_iterator it(text.begin(), text.end(), attributeRegex), end;
         it != end;
         ++it)
    {
        attributes[(*it)[1].str()] = (*it)[2].str();
    }
    return attributes;
}

std::string Trim(std::string value)
{
    const auto isSpace = [](unsigned char character)
    {
        return std::isspace(character) != 0;
    };

    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [isSpace](char character)
            {
                return isSpace(static_cast<unsigned char>(character)) == false;
            }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [isSpace](char character)
            {
                return isSpace(static_cast<unsigned char>(character)) == false;
            }).base(),
        value.end());
    return value;
}

std::string ToLower(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool TryReadStyleProperty(
    const std::string& style,
    const std::string& propertyName,
    std::string& outValue)
{
    std::size_t cursor = 0u;
    while (cursor < style.size())
    {
        const std::size_t end = style.find(';', cursor);
        const std::size_t tokenEnd = end == std::string::npos ? style.size() : end;
        const std::string declaration = style.substr(cursor, tokenEnd - cursor);
        const std::size_t separator = declaration.find(':');
        if (separator != std::string::npos)
        {
            const std::string name = ToLower(Trim(declaration.substr(0u, separator)));
            if (name == propertyName)
            {
                outValue = Trim(declaration.substr(separator + 1u));
                return true;
            }
        }

        if (end == std::string::npos)
        {
            break;
        }
        cursor = end + 1u;
    }
    return false;
}

bool TryParseFillRule(
    const std::string& text,
    VectorFillRule& outFillRule)
{
    const std::string value = ToLower(Trim(text));
    if (value == "nonzero")
    {
        outFillRule = VectorFillRule::NonZero;
        return true;
    }
    if (value == "evenodd")
    {
        outFillRule = VectorFillRule::EvenOdd;
        return true;
    }
    return false;
}

} // namespace

bool SvgPathStyleImporter::ApplyFilePathStyles(
    const std::string& path,
    VectorDocument& document,
    std::string* outError)
{
    std::string source;
    if (ReadTextFile(path, source) == false)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open SVG file while importing path styles: " + path;
        }
        return false;
    }

    const std::regex pathRegex(
        R"(<path\b([^>]*?)(?:/>|>[\s\S]*?</path>))",
        std::regex::icase);

    std::size_t pathIndex = 0u;
    for (std::sregex_iterator it(source.begin(), source.end(), pathRegex), end;
         it != end;
         ++it)
    {
        if (pathIndex >= document.Paths.size())
        {
            if (outError != nullptr)
            {
                *outError = "SVG path style count does not match imported path geometry count.";
            }
            return false;
        }

        PathElement& pathElement = document.Paths[pathIndex++];
        pathElement.FillRule = VectorFillRule::NonZero;

        const AttributeMap attributes = ParseAttributes((*it)[1].str());
        std::string fillRuleText;

        // SVG presentation attributeよりinline styleの同名propertyを優先します。
        // 完全なCSS cascadeはこのImporterの責務外ですが、一般的な
        // fill-rule="..." と style="fill-rule:..." の両表現を扱います。
        const auto fillRuleIt = attributes.find("fill-rule");
        if (fillRuleIt != attributes.end())
        {
            fillRuleText = fillRuleIt->second;
        }

        const auto styleIt = attributes.find("style");
        if (styleIt != attributes.end())
        {
            std::string styleValue;
            if (TryReadStyleProperty(styleIt->second, "fill-rule", styleValue) == true)
            {
                fillRuleText = styleValue;
            }
        }

        if (fillRuleText.empty() == true)
        {
            continue;
        }

        if (TryParseFillRule(fillRuleText, pathElement.FillRule) == false)
        {
            if (outError != nullptr)
            {
                *outError = "Unsupported SVG fill-rule value: " + fillRuleText;
            }
            return false;
        }
    }

    if (pathIndex != document.Paths.size())
    {
        if (outError != nullptr)
        {
            *outError = "SVG path style count does not match imported path geometry count.";
        }
        return false;
    }

    return true;
}

} // namespace Raven
