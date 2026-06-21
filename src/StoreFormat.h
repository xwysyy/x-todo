#pragma once

#include "Store.h"

#include <string>

// Pure persistence-format helpers for XTODO data.txt.
// They intentionally avoid Win32 so format migrations and UI-state parsing can be unit-tested.
namespace StoreFormat {

std::wstring Escape(const std::wstring& in);
std::wstring Unescape(const std::wstring& in);

// Parses already-decoded UTF-16/UTF-32 text. Corruption detection (for example, a
// non-empty file that does not start with XTODO) remains in Store::Load because it
// controls backup behavior.
bool ParseText(const std::wstring& text, TodoModel& model, WindowGeometry& geom, UiState& ui);

// Serializes the current model and UI state to the latest on-disk format.
std::wstring SerializeText(const TodoModel& model, const WindowGeometry& geom, const UiState& ui);

} // namespace StoreFormat
