#include "Raven/Core/Containers/Tests/StringSelfTests.h"

#include "Raven/Core/Containers/String.h"
#include "Raven/Core/Containers/StringView.h"
#include "Raven/Core/Memory/HeapAllocator.h"

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace Raven::tests
{

void RunStringSelfTests()
{
    // Viewは所有せず、embedded nullを含むbyte列も長さを失わず扱います。
    const char bytes[] = { 'R', 'a', '\0', 'v', 'e', 'n' };
    StringView view(bytes, sizeof(bytes));
    assert(view.size() == 6);
    assert(view[2] == '\0');
    assert(view.substr(3) == StringView("ven"));
    assert(view.starts_with(StringView("Ra")));
    assert(view.ends_with(StringView("ven")));
    assert(view.find('\0') == 2);
    assert(view.find('x') == StringView::npos);

    StringView trimmed("prefix-value-suffix");
    trimmed.remove_prefix(7);
    trimmed.remove_suffix(7);
    assert(trimmed == StringView("value"));
    assert(StringView().ToStdStringView().empty());

    HeapAllocator sourceAllocator;
    HeapAllocator destinationAllocator;
    {
        String empty(sourceAllocator);
        assert(empty.empty());
        assert(empty.GetAllocator() == &sourceAllocator);
        assert(empty.c_str()[0] == '\0');

        // UTF-8は正規化や文字単位変換を暗黙に行わず、入力byte列を保持します。
        const std::string utf8 = u8"Raven文字列";
        String text(sourceAllocator, std::string_view(utf8));
        assert(text.size() == utf8.size());
        assert(text.ToStdStringView() == std::string_view(utf8));
        assert(text.view() == StringView(utf8));

        text += StringView(" API");
        text += '!';
        assert(text.view().ends_with(StringView(" API!")));

        String embedded(sourceAllocator, bytes, sizeof(bytes));
        assert(embedded.size() == sizeof(bytes));
        assert(embedded[2] == '\0');

        const std::string longText(256, 'x');
        const std::size_t allocationsBeforeLongText = sourceAllocator.GetAllocationCount();
        String source(sourceAllocator, std::string_view(longText));
        assert(sourceAllocator.GetAllocationCount() > allocationsBeforeLongText);

        String copied(source);
        assert(copied == source);
        assert(copied.GetAllocator() == &sourceAllocator);

        String moved(std::move(copied));
        assert(moved == StringView(longText));
        assert(moved.GetAllocator() == &sourceAllocator);

        String destination(destinationAllocator, "old");
        destination = source;
        assert(destination == StringView(longText));
        // 代入は呼び出し側が選択した移動先Allocatorを変更しません。
        assert(destination.GetAllocator() == &destinationAllocator);

        String moveDestination(destinationAllocator, "discarded");
        moveDestination = std::move(source);
        assert(moveDestination == StringView(longText));
        assert(moveDestination.GetAllocator() == &destinationAllocator);

        moveDestination.resize(260, '!');
        assert(moveDestination.size() == 260);
        moveDestination.pop_back();
        assert(moveDestination.size() == 259);
        moveDestination.clear();
        assert(moveDestination.empty());
    }
    assert(sourceAllocator.GetAllocationCount() == 0);
    assert(destinationAllocator.GetAllocationCount() == 0);
}

} // namespace Raven::tests
