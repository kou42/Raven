#include "ProjectSync.h"
#include "GitUpdate.h"
#include "ProcessRunner.h"

#include <Shlwapi.h>
#include <xmllite.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <winrt/base.h>

namespace RavenLauncher
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::wstring_view MsbuildNamespace = L"http://schemas.microsoft.com/developer/msbuild/2003";
        constexpr size_t NoParent = size_t(-1);

        struct PathLess
        {
            bool operator()(const std::wstring& left, const std::wstring& right) const
            {
                return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
            }
        };

        struct Node
        {
            std::wstring name;
            std::map<std::wstring, std::wstring> attributes;
            std::wstring text;
            size_t parent = NoParent;
            size_t firstLine = 0;
            size_t lastLine = 0;
            size_t children = 0;
            bool conditional = false;
            bool complex = false;

            std::wstring Attribute(const wchar_t* key) const
            {
                const auto found = attributes.find(key);
                return found == attributes.end() ? L"" : found->second;
            }
        };

        struct Document
        {
            std::string bytes;
            std::vector<Node> nodes;
            std::vector<size_t> lines{0};

            size_t Offset(size_t line) const
            {
                if (line == 0 || line > lines.size())
                {
                    throw std::runtime_error("XML parser returned an invalid line number.");
                }
                return lines[line - 1];
            }
        };

        bool IsReparsePoint(const fs::path& path)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }

        std::string ReadBytes(const fs::path& path)
        {
            if (IsReparsePoint(path))
            {
                throw std::runtime_error("Project XML must be a regular file.");
            }
            std::ifstream stream(path, std::ios::binary);
            if (stream.is_open() == false)
            {
                throw std::runtime_error("Could not read project XML: " + winrt::to_string(path.filename().native()));
            }
            return {std::istreambuf_iterator<char>(stream), {}};
        }

        Document Parse(std::string bytes)
        {
            // バイト範囲で編集するため、UTF-8 の Visual Studio XML のみを扱う。
            if (bytes.empty() || bytes.contains('\0') || bytes.size() > UINT_MAX)
            {
                throw std::runtime_error("Project XML must be UTF-8.");
            }
            winrt::to_hstring(bytes);
            Document document;
            document.bytes = std::move(bytes);
            for (size_t index = 0; index < document.bytes.size(); ++index)
            {
                if (document.bytes[index] == '\n')
                {
                    document.lines.push_back(index + 1);
                }
            }

            winrt::com_ptr<IStream> stream;
            stream.attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(document.bytes.data()),
                static_cast<UINT>(document.bytes.size())));
            winrt::check_pointer(stream.get());
            winrt::com_ptr<IXmlReader> reader;
            winrt::check_hresult(CreateXmlReader(__uuidof(IXmlReader), reader.put_void(), nullptr));
            winrt::check_hresult(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
            winrt::check_hresult(reader->SetInput(stream.get()));
            std::vector<size_t> stack;
            XmlNodeType type{};
            HRESULT status = S_OK;
            while ((status = reader->Read(&type)) == S_OK)
            {
                UINT line = 0;
                winrt::check_hresult(reader->GetLineNumber(&line));
                if (type == XmlNodeType_Element)
                {
                    const wchar_t* name = nullptr;
                    const wchar_t* ns = nullptr;
                    const wchar_t* prefix = nullptr;
                    winrt::check_hresult(reader->GetLocalName(&name, nullptr));
                    winrt::check_hresult(reader->GetNamespaceUri(&ns, nullptr));
                    winrt::check_hresult(reader->GetPrefix(&prefix, nullptr));
                    const bool recognized = name != nullptr && ns != nullptr && std::wstring_view(ns) == MsbuildNamespace &&
                        (prefix == nullptr || *prefix == L'\0');
                    Node node;
                    node.name = recognized ? name : L"";
                    node.firstLine = line;
                    node.lastLine = line;
                    if (stack.empty() == false)
                    {
                        node.parent = stack.back();
                        ++document.nodes[node.parent].children;
                        node.conditional = document.nodes[node.parent].conditional ||
                            (document.nodes[node.parent].name != L"Project" && document.nodes[node.parent].name != L"ItemGroup");
                    }
                    const BOOL empty = reader->IsEmptyElement();
                    HRESULT attribute = reader->MoveToFirstAttribute();
                    while (attribute == S_OK)
                    {
                        const wchar_t* key = nullptr;
                        const wchar_t* value = nullptr;
                        winrt::check_hresult(reader->GetQualifiedName(&key, nullptr));
                        const std::wstring attributeName(key);
                        winrt::check_hresult(reader->GetValue(&value, nullptr));
                        node.attributes.emplace(attributeName, value == nullptr ? L"" : value);
                        attribute = reader->MoveToNextAttribute();
                    }
                    winrt::check_hresult(attribute);
                    winrt::check_hresult(reader->MoveToElement());
                    node.conditional = node.conditional || node.attributes.contains(L"Condition");
                    if (empty == FALSE)
                    {
                        stack.push_back(document.nodes.size());
                    }
                    document.nodes.push_back(std::move(node));
                }
                else if (stack.empty() == false)
                {
                    auto& current = document.nodes[stack.back()];
                    if (type == XmlNodeType_EndElement)
                    {
                        current.lastLine = line;
                        stack.pop_back();
                    }
                    else if (type == XmlNodeType_Text)
                    {
                        const wchar_t* value = nullptr;
                        winrt::check_hresult(reader->GetValue(&value, nullptr));
                        if (value != nullptr)
                        {
                            current.text += value;
                        }
                    }
                    else if (type != XmlNodeType_Whitespace)
                    {
                        current.complex = true;
                    }
                }
            }
            winrt::check_hresult(status);
            if (document.nodes.empty() || document.nodes.front().name != L"Project")
            {
                throw std::runtime_error("Project XML does not use the expected MSBuild namespace.");
            }
            return document;
        }

        std::wstring Normalize(std::wstring value)
        {
            std::ranges::replace(value, L'/', L'\\');
            // 一部の既存 filters はパス区切りを重ねて記録している。
            std::wstring normalized;
            for (const wchar_t character : value)
            {
                if (character != L'\\' || normalized.empty() || normalized.back() != L'\\')
                {
                    normalized += character;
                }
            }
            return normalized;
        }

        bool IsSource(const std::wstring& value)
        {
            if (value.size() <= 6 || CompareStringOrdinal(value.c_str(), 6, L"Raven\\", 6, TRUE) != CSTR_EQUAL ||
                value.find_first_of(L":*?;$@%") != std::wstring::npos)
            {
                return false;
            }
            const fs::path path(value);
            for (const auto& part : path)
            {
                if (part == L".." || part == L"." || _wcsicmp(part.c_str(), L"Assets") == 0 ||
                    _wcsicmp(part.c_str(), L"Generated") == 0 || _wcsicmp(part.c_str(), L"ThirdParty") == 0)
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring FileType(const std::wstring& value)
        {
            auto extension = fs::path(value).extension().native();
            std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (extension == L".cpp" || extension == L".c")
            {
                return L"ClCompile";
            }
            if (extension == L".h" || extension == L".hpp")
            {
                return L"ClInclude";
            }
            return extension == L".inl" ? L"None" : L"";
        }

        std::wstring Escape(std::wstring_view value)
        {
            std::wstring result;
            for (const auto c : value)
            {
                switch (c)
                {
                case L'&': result += L"&amp;"; break;
                case L'<': result += L"&lt;"; break;
                case L'>': result += L"&gt;"; break;
                case L'"': result += L"&quot;"; break;
                default: result += c; break;
                }
            }
            return result;
        }

        struct Range
        {
            size_t begin;
            size_t end;
        };

        std::optional<Range> WholeLines(const Document& xml, const Node& node)
        {
            const auto begin = xml.Offset(node.firstLine);
            const auto end = node.lastLine < xml.lines.size() ? xml.Offset(node.lastLine + 1) : xml.bytes.size();
            auto text = std::string_view(xml.bytes).substr(begin, end - begin);
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            if (first == std::string::npos || text.substr(first).starts_with("<" + winrt::to_string(node.name)) == false)
            {
                return std::nullopt;
            }
            // 編集範囲に隣のタグや行末コメントがあると、その行は手動確認に回す。
            const auto closing = node.firstLine == node.lastLine && text.substr(first, last - first + 1).ends_with("/>") ?
                std::string("/>") : "</" + winrt::to_string(node.name) + ">";
            if (text.find(closing, first) != last + 1 - closing.size())
            {
                return std::nullopt;
            }
            return Range{begin, end};
        }

        bool PlainItem(const Document& xml, const Node& node)
        {
            return node.conditional == false && node.complex == false && node.parent != NoParent &&
                xml.nodes[node.parent].name == L"ItemGroup" && xml.nodes[node.parent].parent == 0 &&
                node.attributes.size() == 1 && node.attributes.contains(L"Include") && node.text.empty();
        }

        const Node* FilterChild(const Document& xml, const Node& item)
        {
            if (item.children != 1)
            {
                return nullptr;
            }
            const auto index = static_cast<size_t>(&item - xml.nodes.data());
            const auto& child = xml.nodes.at(index + 1);
            if (child.name != L"Filter" || child.attributes.empty() == false || child.children != 0 ||
                child.complex || child.text.empty() || child.text.find_first_of(L"\r\n") != std::wstring::npos)
            {
                return nullptr;
            }
            return &child;
        }

        struct Project
        {
            fs::path props;
            fs::path filters;
            Document items;
            Document display;
            std::map<std::wstring, std::wstring> mapping;
            std::map<std::wstring, std::vector<size_t>, PathLess> filterItems;
            std::set<std::wstring, PathLess> declaredFilters;
            std::vector<Range> removeItems;
            std::vector<Range> removeFilters;
            std::wstring addItems;
            std::wstring addFilters;
        };

        using Paths = std::set<std::wstring, PathLess>;

        void ReservePaths(const Node& node, Paths& paths)
        {
            for (const auto* attribute : {L"Include", L"Update", L"Remove", L"Exclude"})
            {
                const auto list = node.Attribute(attribute);
                size_t offset = 0;
                while (offset < list.size())
                {
                    const auto delimiter = list.find(L';', offset);
                    const auto value = Normalize(list.substr(offset, delimiter - offset));
                    if (IsSource(value))
                    {
                        paths.insert(value);
                    }
                    if (delimiter == std::wstring::npos)
                    {
                        break;
                    }
                    offset = delimiter + 1;
                }
            }
        }

        Project ReadProject(const fs::path& directory, std::wstring_view name, Paths& reserved, Paths& overrides)
        {
            const bool root = name == L"Root";
            const std::wstring stem = root ? L"Root" : L"Raven." + std::wstring(name);
            const std::wstring propsName = L"Raven." + std::wstring(name) + L".Items.props";
            const std::wstring itemPrefix = L"Raven" + std::wstring(name);
            Project project;
            project.props = directory / propsName;
            project.filters = directory / (stem + L".vcxproj.filters");
            const auto definition = Parse(ReadBytes(directory / (stem + L".vcxproj")));
            size_t imports = 0;
            for (const auto& node : definition.nodes)
            {
                const auto import = node.Attribute(L"Project");
                if (node.name == L"Import" && import == propsName)
                {
                    if (node.parent != 0 || node.conditional)
                    {
                        throw std::runtime_error("A conditional Items.props import needs manual review.");
                    }
                    ++imports;
                }
                if (node.parent != NoParent && definition.nodes[node.parent].name == L"ItemGroup")
                {
                    ReservePaths(node, reserved);
                    ReservePaths(node, overrides);
                }
                for (const auto& [suffix, type] : {std::pair{L"Source", L"ClCompile"},
                    std::pair{L"Header", L"ClInclude"}, std::pair{L"Inline", L"None"}})
                {
                    const auto tag = itemPrefix + suffix;
                    if (node.Attribute(L"Include") == L"@(" + tag + L")" &&
                        node.name == type && PlainItem(definition, node) && node.children == 0)
                    {
                        project.mapping.emplace(tag, type);
                    }
                }
            }
            if (imports != 1 || project.mapping.contains(itemPrefix + L"Source") == false ||
                project.mapping.contains(itemPrefix + L"Header") == false)
            {
                throw std::runtime_error("Project Items.props imports or item mappings are unsupported.");
            }

            project.items = Parse(ReadBytes(project.props));
            project.display = Parse(ReadBytes(project.filters));
            for (size_t index = 0; index < project.display.nodes.size(); ++index)
            {
                const auto& node = project.display.nodes[index];
                if (node.name == L"Filter" && node.attributes.contains(L"Include") == false)
                {
                    continue;
                }
                if (node.name == L"Filter" && node.attributes.contains(L"Include"))
                {
                    if (node.conditional == false && node.parent != NoParent &&
                        project.display.nodes[node.parent].name == L"ItemGroup")
                    {
                        project.declaredFilters.insert(Normalize(node.Attribute(L"Include")));
                    }
                    continue;
                }
                ReservePaths(node, reserved);
                if (node.attributes.contains(L"Update") || node.attributes.contains(L"Remove"))
                {
                    ReservePaths(node, overrides);
                }
                const auto path = Normalize(node.Attribute(L"Include"));
                if (IsSource(path))
                {
                    project.filterItems[path].push_back(index);
                }
            }
            return project;
        }

        struct Owner
        {
            size_t project;
            std::wstring filter;

            bool operator==(const Owner&) const = default;
        };

        struct Change
        {
            size_t project;
            std::wstring path;
        };

        std::string EditDocument(const Document& xml, std::vector<Range> ranges, const std::wstring& additions)
        {
            const auto newline = xml.bytes.contains("\r\n") ? L"\r\n" : L"\n";
            auto result = xml.bytes;
            if (additions.empty() == false)
            {
                const auto close = xml.Offset(xml.nodes.front().lastLine);
                if (std::string_view(xml.bytes).substr(close).starts_with("</Project>") == false)
                {
                    throw std::runtime_error("The closing Project tag must start its own line.");
                }
                result.insert(close, winrt::to_string(std::wstring(L"  <ItemGroup>") + newline +
                    additions + L"  </ItemGroup>" + newline));
            }
            std::ranges::sort(ranges, std::greater{}, &Range::begin);
            for (const auto& range : ranges)
            {
                result.erase(range.begin, range.end - range.begin);
            }
            return result;
        }

        bool HasSymlink(const fs::path& base, const fs::path& relative)
        {
            auto current = base;
            for (const auto& part : relative)
            {
                current /= part;
                if (IsReparsePoint(current))
                {
                    return true;
                }
            }
            return false;
        }

        void ThrowIfCancelled(std::stop_token stop)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Operation cancelled.");
            }
        }
    }

    ProjectSyncPlan PreviewProjectSync(const fs::path& location, std::stop_token stop)
    {
        const auto git = FindGit();
        auto root = FindGitRoot(git, location, stop);
        const auto directory = root / L"Root/Root";
        if (HasSymlink(root, L"Root/Root/Raven") || fs::is_directory(directory / L"Raven") == false)
        {
            throw std::runtime_error("The Raven source directory is missing or is a reparse point.");
        }

        Paths reserved;
        Paths overrides;
        for (const auto* name : {L"Raven.Library.props", L"Raven.Common.props", L"RavenUITest.props"})
        {
            const auto xml = Parse(ReadBytes(directory / name));
            for (const auto& node : xml.nodes)
            {
                if (node.parent != NoParent && xml.nodes[node.parent].name == L"ItemGroup")
                {
                    ReservePaths(node, overrides);
                    ReservePaths(node, reserved);
                }
            }
        }

        std::vector<Project> projects;
        for (const auto name : {L"Root", L"Audio", L"Data", L"Diagnostics", L"Editor",
            L"Foundation", L"Graphics", L"Network", L"Runtime"})
        {
            ThrowIfCancelled(stop);
            projects.push_back(ReadProject(directory, name, reserved, overrides));
        }

        std::map<std::wstring, size_t, PathLess> registrations;
        std::map<std::wstring, std::vector<Owner>, PathLess> owners;
        for (size_t index = 0; index < projects.size(); ++index)
        {
            auto& project = projects[index];
            for (const auto& item : project.items.nodes)
            {
                ReservePaths(item, reserved);
                if (item.attributes.contains(L"Update") || item.attributes.contains(L"Remove") ||
                    project.mapping.contains(item.name) == false)
                {
                    ReservePaths(item, overrides);
                }
                const auto path = Normalize(item.Attribute(L"Include"));
                if (IsSource(path) == false || project.mapping.contains(item.name) == false)
                {
                    continue;
                }
                ++registrations[path];
                auto& candidates = owners[fs::path(path).parent_path().native()];
                const auto filter = project.filterItems.find(path);
                std::wstring logical;
                if (PlainItem(project.items, item) && item.children == 0 &&
                    filter != project.filterItems.end() && filter->second.size() == 1)
                {
                    const auto& entry = project.display.nodes[filter->second.front()];
                    const auto* child = FilterChild(project.display, entry);
                    if (PlainItem(project.display, entry) && entry.name == project.mapping.at(item.name) &&
                        child != nullptr && project.declaredFilters.contains(Normalize(child->text)))
                    {
                        logical = Normalize(child->text);
                    }
                }
                const Owner owner{index, std::move(logical)};
                if (std::ranges::find(candidates, owner) == candidates.end())
                {
                    candidates.push_back(owner);
                }
            }
        }

        std::wstring manual;
        std::vector<Change> removals;
        for (size_t index = 0; index < projects.size(); ++index)
        {
            ThrowIfCancelled(stop);
            auto& project = projects[index];
            for (const auto& item : project.items.nodes)
            {
                const auto path = Normalize(item.Attribute(L"Include"));
                if (IsSource(path) == false || project.mapping.contains(item.name) == false ||
                    fs::exists(directory / path))
                {
                    continue;
                }
                const auto display = project.filterItems.find(path);
                const Node* filter = nullptr;
                if (display != project.filterItems.end() && display->second.size() == 1)
                {
                    filter = &project.display.nodes[display->second.front()];
                }
                if (PlainItem(project.items, item) && item.children == 0 && filter != nullptr &&
                    PlainItem(project.display, *filter) && filter->name == project.mapping.at(item.name) &&
                    (filter->children == 0 || FilterChild(project.display, *filter) != nullptr) &&
                    registrations.at(path) == 1 && overrides.contains(path) == false &&
                    HasSymlink(directory, path) == false)
                {
                    const auto itemRange = WholeLines(project.items, item);
                    const auto filterRange = WholeLines(project.display, *filter);
                    if (itemRange.has_value() && filterRange.has_value())
                    {
                        project.removeItems.push_back(*itemRange);
                        project.removeFilters.push_back(*filterRange);
                        removals.push_back({index, path});
                        continue;
                    }
                }
                manual += L"\r\n  Missing, review manually: " + path;
            }
        }

        using namespace std::chrono_literals;
        const auto sourceFiles = RunProcess(git, {L"ls-files", L"--cached", L"--others", L"--exclude-standard",
            L"-z", L"--", L"Root/Root/Raven/"}, root, stop, 30s);
        if (sourceFiles.exitCode != 0)
        {
            throw std::runtime_error("Could not enumerate Raven source files.");
        }
        std::vector<Change> additions;
        for (size_t begin = 0; begin < sourceFiles.output.size();)
        {
            ThrowIfCancelled(stop);
            const auto end = sourceFiles.output.find('\0', begin);
            constexpr std::string_view prefix = "Root/Root/";
            const auto entry = std::string_view(sourceFiles.output).substr(begin, end - begin);
            if (end == std::string::npos || entry.starts_with(prefix) == false)
            {
                throw std::runtime_error("Git returned an invalid source path.");
            }
            begin = end + 1;
            const auto path = Normalize(winrt::to_hstring(entry.substr(prefix.size())).c_str());
            if (IsSource(path) == false || reserved.insert(path).second == false ||
                HasSymlink(directory, path) || fs::is_regular_file(directory / path) == false)
            {
                continue;
            }
            const auto type = FileType(path);
            if (type.empty())
            {
                continue;
            }
            const auto found = owners.find(fs::path(path).parent_path().native());
            if (found != owners.end())
            {
                const auto& candidates = found->second;
                const auto& first = candidates.front();
                Paths logicalFilters;
                bool sameProject = true;
                for (const auto& candidate : candidates)
                {
                    sameProject = sameProject && first.project == candidate.project;
                    if (candidate.filter.empty() == false)
                    {
                        logicalFilters.insert(candidate.filter);
                    }
                }
                auto& project = projects[first.project];
                const auto mapping = std::ranges::find_if(project.mapping,
                    [&](const auto& pair) { return pair.second == type; });
                if (sameProject && logicalFilters.size() == 1 && mapping != project.mapping.end())
                {
                    const auto itemNewline = project.items.bytes.contains("\r\n") ? L"\r\n" : L"\n";
                    const auto filterNewline = project.display.bytes.contains("\r\n") ? L"\r\n" : L"\n";
                    const auto attribute = L" Include=\"" + Escape(path) + L"\"";
                    project.addItems += L"    <" + mapping->first + attribute + L" />" + itemNewline;
                    project.addFilters += L"    <" + type + attribute + L"><Filter>" + Escape(*logicalFilters.begin()) +
                        L"</Filter></" + type + L">" + filterNewline;
                    additions.push_back({first.project, path});
                    continue;
                }
            }
            manual += L"\r\n  Unregistered, review manually: " + path;
        }

        ProjectSyncPlan plan{std::move(root), {}, L"Visual Studio project sync preview"};
        std::vector<bool> paired(removals.size(), false);
        const auto sameName = [](const Change& left, const Change& right)
        {
            return left.project == right.project &&
                _wcsicmp(fs::path(left.path).filename().c_str(), fs::path(right.path).filename().c_str()) == 0;
        };
        for (const auto& addition : additions)
        {
            const auto name = projects[addition.project].props.filename().native();
            if (std::ranges::count_if(additions, [&](const auto& other) { return sameName(addition, other); }) == 1 &&
                std::ranges::count_if(removals, [&](const auto& other) { return sameName(addition, other); }) == 1)
            {
                const auto match = std::ranges::find_if(removals, [&](const auto& remove) { return sameName(addition, remove); });
                paired[static_cast<size_t>(match - removals.begin())] = true;
                plan.preview += L"\r\n  Possible move [" + name + L"]: " + match->path + L" -> " + addition.path;
            }
            else
            {
                plan.preview += L"\r\n  Add [" + name + L"]: " + addition.path;
            }
        }
        for (size_t index = 0; index < removals.size(); ++index)
        {
            if (paired[index] == false)
            {
                const auto& remove = removals[index];
                plan.preview += L"\r\n  Remove [" + projects[remove.project].props.filename().native() + L"]: " + remove.path;
            }
        }

        // 曖昧な候補はプレビューに残しても、保存する XML 差分には含めない。
        plan.preview += manual;
        plan.needsReview = manual.empty() == false;
        for (auto& project : projects)
        {
            if (project.removeItems.empty() && project.addItems.empty())
            {
                continue;
            }
            auto items = EditDocument(project.items, std::move(project.removeItems), project.addItems);
            auto filters = EditDocument(project.display, std::move(project.removeFilters), project.addFilters);
            Parse(items);
            Parse(filters);
            plan.files.push_back({project.props.lexically_relative(plan.root), std::move(project.items.bytes), std::move(items)});
            plan.files.push_back({project.filters.lexically_relative(plan.root), std::move(project.display.bytes), std::move(filters)});
        }
        if (plan.files.empty())
        {
            plan.preview += L"\r\nNo automatic changes.";
        }
        return plan;
    }

    namespace
    {
        void WriteBytes(const fs::path& path, const std::string& bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.close();
            if (stream.fail())
            {
                throw std::runtime_error("Unable to write project sync staging or backup file.");
            }
        }

        std::wstring UniqueId()
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            wchar_t value[40];
            StringFromGUID2(id, value, static_cast<int>(std::size(value)));
            return value;
        }

        fs::path StagedPath(const fs::path& destination, const std::wstring& id)
        {
            return destination.native() + L"." + id + L".tmp";
        }

        void Replace(const fs::path& staging, const fs::path& destination)
        {
            if (MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
            {
                ThrowWindowsError("Replace project XML");
            }
        }
    }

    ProjectSyncResult ApplyProjectSync(const ProjectSyncPlan& plan, const fs::path& backupRoot)
    {
        if (plan.files.empty())
        {
            return {true, L"No automatic changes."};
        }
        // 別のランチャーからの反映とバックアップ作成が同時に走らないようにする。
        winrt::handle mutex{CreateMutexW(nullptr, FALSE, L"Local\\RavenLauncher.ProjectSync")};
        if (mutex.get() == nullptr)
        {
            ThrowWindowsError("Create project sync mutex");
        }
        const DWORD wait = WaitForSingleObject(mutex.get(), 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        {
            throw std::runtime_error("Another project sync is active or the lock is unavailable.");
        }
        struct MutexRelease
        {
            HANDLE handle;
            ~MutexRelease() { ReleaseMutex(handle); }
        } lock{mutex.get()};
        // 確認後に XML またはソースツリーが変わったプレビューは反映しない。
        if (PreviewProjectSync(plan.root, {}) != plan)
        {
            throw std::runtime_error("The project or source tree changed after preview. Preview the changes again.");
        }
        const auto id = UniqueId();
        const auto backup = backupRoot / id;
        fs::create_directories(backup);
        const auto backupLabel = L"\r\nBackup: %LOCALAPPDATA%\\RavenLauncher\\sync-backups\\" + id;
        struct StagingCleanup
        {
            std::vector<fs::path> paths;
            ~StagingCleanup()
            {
                for (const auto& path : paths)
                {
                    DeleteFileW(path.c_str());
                }
            }
        } staging;
        size_t replaced = 0;
        try
        {
            for (const auto& file : plan.files)
            {
                auto stage = StagedPath(plan.root / file.path, id);
                staging.paths.push_back(stage);
                WriteBytes(backup / file.path.filename(), file.before);
                WriteBytes(stage, file.after);
            }
            for (const auto& file : plan.files)
            {
                const auto destination = plan.root / file.path;
                if (ReadBytes(destination) != file.before)
                {
                    throw std::runtime_error("A project file changed during synchronization.");
                }
                Replace(staging.paths[replaced], destination);
                ++replaced;
            }
            // 作成した XML を実ファイルから再解析し、同じソースツリーに対して差分が消えることを確認する。
            if (PreviewProjectSync(plan.root, {}).files.empty() == false)
            {
                throw std::runtime_error("Project synchronization did not converge.");
            }
        }
        catch (...)
        {
            std::wstring failed;
            while (replaced > 0)
            {
                --replaced;
                const auto& file = plan.files[replaced];
                const auto destination = plan.root / file.path;
                try
                {
                    if (ReadBytes(destination) != file.after)
                    {
                        throw std::runtime_error("The updated file was changed externally.");
                    }
                    WriteBytes(staging.paths[replaced], file.before);
                    Replace(staging.paths[replaced], destination);
                }
                catch (...)
                {
                    failed += L"\r\n  " + file.path.native();
                }
            }
            std::wstring reason = L"Project sync failed.";
            try
            {
                throw;
            }
            catch (const winrt::hresult_error& error)
            {
                reason += L" ";
                reason += error.message();
            }
            catch (const std::system_error& error)
            {
                reason += L" Windows error " + std::to_wstring(error.code().value()) + L".";
            }
            catch (const std::exception& error)
            {
                reason += L" ";
                reason += winrt::to_hstring(error.what());
            }
            reason += failed.empty() ? L"\r\nAny replaced project files were restored." :
                L"\r\nRestore these files from the backup:" + failed;
            return {false, reason + backupLabel};
        }
        return {true, L"Updated and verified " + std::to_wstring(plan.files.size()) + L" project files." + backupLabel};
    }
}
