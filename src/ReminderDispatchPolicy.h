#pragma once

#include <initializer_list>

struct ReminderChannelResult {
    bool attempted = false;
    bool accepted = false;
    bool durable = false;
};

struct ReminderDispatchResult {
    bool markFired = false;
};

inline ReminderDispatchResult DecideReminderDispatch(
    std::initializer_list<ReminderChannelResult> channels,
    bool requireDurable = false) {
    ReminderDispatchResult result;
    for (const ReminderChannelResult& channel : channels) {
        if (channel.attempted && channel.accepted && (!requireDurable || channel.durable)) {
            result.markFired = true;
            break;
        }
    }
    return result;
}
