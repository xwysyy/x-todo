#include "EditIntent.h"

namespace GuiEdit {

Intent KeyDownIntent(Key key, bool shiftDown) {
    switch (key) {
        case Key::Enter:
            return Intent::CommitAndAddNext;
        case Key::Escape:
            return Intent::Cancel;
        case Key::Tab:
            return shiftDown ? Intent::Outdent : Intent::Indent;
        case Key::DeleteKey:
            return Intent::RefreshAfterDefault;
        case Key::Other:
            break;
    }
    return Intent::None;
}

bool SuppressChar(wchar_t ch) {
    return ch == L'\t' || ch == L'\r' || ch == 0x1B;
}

} // namespace GuiEdit
