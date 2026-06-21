#pragma once

namespace GuiEdit {

enum class Key {
    Other,
    Enter,
    Escape,
    Tab,
    DeleteKey,
};

enum class Intent {
    None,
    CommitAndAddNext,
    Cancel,
    Indent,
    Outdent,
    RefreshAfterDefault,
};

Intent KeyDownIntent(Key key, bool shiftDown);
bool SuppressChar(wchar_t ch);

} // namespace GuiEdit
