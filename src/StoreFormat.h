#pragma once

#include "Store.h"

#include <string>
#include <vector>

// Pure persistence-format helpers for XTODO data.json.
// They avoid Win32 so the JSON schema and UI-state validation can be unit-tested.
namespace StoreFormat {

// Parses UTF-8 JSON bytes into the model and UI state. Returns false when the
// bytes are not valid JSON, are nested too deeply, or are not a JSON object; on
// false the caller keeps the original file (Store backs it up) instead of
// overwriting it. Empty or whitespace-only input loads safe defaults and
// succeeds. Recognized fields are validated and clamped; unknown or malformed
// fields fall back to defaults.
bool Parse(const std::string& utf8, TodoModel& model, CalendarModel& calendar,
           WindowGeometry& geom, UiState& ui);
bool Parse(const std::string& utf8, TodoModel& model, CalendarModel& calendar,
           WindowGeometry& geom, UiState& ui, std::vector<ReminderLogEntry>& reminderLog);

// Serializes the current model and UI state to pretty-printed UTF-8 JSON.
std::string Serialize(const TodoModel& model, const CalendarModel& calendar,
                      const WindowGeometry& geom, const UiState& ui);
std::string Serialize(const TodoModel& model, const CalendarModel& calendar,
                      const WindowGeometry& geom, const UiState& ui,
                      const std::vector<ReminderLogEntry>& reminderLog);

} // namespace StoreFormat
