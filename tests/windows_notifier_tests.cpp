#include "WindowsNotifier.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void ToastLaunchArgsOpenCalendarBlock() {
    EXPECT_EQ(WindowsNotifier::ToastLaunchArgsForBlock("2026-06-25", 42),
              std::wstring(L"--open-calendar --day 2026-06-25 --block 42"));
    EXPECT_TRUE(WindowsNotifier::ToastLaunchArgsForBlock("2026-06-25", 0).empty());
    EXPECT_TRUE(WindowsNotifier::ToastLaunchArgsForBlock("2026-02-30", 42).empty());
}

void ToastXmlEscapesUserText() {
    EXPECT_EQ(WindowsNotifier::EscapeToastXml(L"Meet <A&B> \"now\""),
              std::wstring(L"Meet &lt;A&amp;B&gt; &quot;now&quot;"));
}

void ToastXmlCarriesLaunchArgsAndEscapedContent() {
    const std::wstring xml =
        WindowsNotifier::ToastXmlForBlock("2026-06-25", 42, L"Starts <soon>",
                                          L"Meet & plan");

    EXPECT_TRUE(xml.find(L"launch=\"--open-calendar --day 2026-06-25 --block 42\"") != std::wstring::npos);
    EXPECT_TRUE(xml.find(L"Starts &lt;soon&gt;") != std::wstring::npos);
    EXPECT_TRUE(xml.find(L"Meet &amp; plan") != std::wstring::npos);
}

const TestCase kTests[] = {
    {"ToastLaunchArgsOpenCalendarBlock", ToastLaunchArgsOpenCalendarBlock},
    {"ToastXmlEscapesUserText", ToastXmlEscapesUserText},
    {"ToastXmlCarriesLaunchArgsAndEscapedContent", ToastXmlCarriesLaunchArgsAndEscapedContent},
};

} // namespace

int main() {
    return RunTests("windows_notifier", kTests);
}
