#include "test_framework.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace xtodo_test;

#ifndef XTODO_SOURCE_DIR
#define XTODO_SOURCE_DIR "."
#endif

namespace {

std::string ReadSourceFile(const std::string& relativePath) {
    const std::string path = std::string(XTODO_SOURCE_DIR) + "/" + relativePath;
    std::ifstream in(path);
    if (!in) Fail(__FILE__, __LINE__, "Could not open source file: " + path);

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void ExpectNoToken(const std::string& file, const std::string& source,
                   const std::string& token) {
    const size_t pos = source.find(token);
    if (pos != std::string::npos) {
        std::ostringstream message;
        message << file << " must not contain GDI rendering token `" << token
                << "` at byte " << pos;
        Fail(__FILE__, __LINE__, message.str());
    }
}

void SettingsLikeWindowsDoNotUseGdiRendering() {
    const std::vector<std::string> files = {
        "src/SettingsWindow.cpp",
        "src/ThemeManagerWindow.cpp",
    };
    const std::vector<std::string> forbidden = {
        "CreateCompatibleDC(",
        "CreateCompatibleBitmap(",
        "BitBlt(",
        "RoundRect(",
        "Ellipse(",
        "CreatePen(",
        "CreateSolidBrush(",
        "DrawTextW(",
        "GetTextExtentPoint32W(",
    };

    for (const std::string& file : files) {
        const std::string source = ReadSourceFile(file);
        for (const std::string& token : forbidden)
            ExpectNoToken(file, source, token);
    }
}

void SharedThemedWindowControlsDoNotExposeGdiDrawingApi() {
    const std::vector<std::string> files = {
        "src/ThemedWindowControls.h",
        "src/ThemedWindowControls.cpp",
    };
    const std::vector<std::string> forbidden = {
        "HDC ",
        "HFONT ",
        "FillRoundColor",
        "StrokeRoundColor",
        "DrawTextInRect",
        "CreateCompatibleDC(",
        "CreateCompatibleBitmap(",
        "BitBlt(",
        "RoundRect(",
        "Ellipse(",
        "CreatePen(",
        "CreateSolidBrush(",
        "::DrawTextW(",
        "GetTextExtentPoint32W(",
        "CreateFontIndirectW(",
    };

    for (const std::string& file : files) {
        const std::string source = ReadSourceFile(file);
        for (const std::string& token : forbidden)
            ExpectNoToken(file, source, token);
    }
}

const TestCase kTests[] = {
    {"SettingsLikeWindowsDoNotUseGdiRendering", SettingsLikeWindowsDoNotUseGdiRendering},
    {"SharedThemedWindowControlsDoNotExposeGdiDrawingApi", SharedThemedWindowControlsDoNotExposeGdiDrawingApi},
};

} // namespace

int main() {
    return RunTests("rendering_policy", kTests);
}
