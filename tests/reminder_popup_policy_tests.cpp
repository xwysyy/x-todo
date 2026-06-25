#include "ReminderPopupPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void PopupPlacementUsesWorkAreaBottomRight() {
    const ReminderPopupWorkArea work{100, 50, 1500, 950};

    const ReminderPopupPlacement placement = ComputeReminderPopupPlacement(work, 144);

    EXPECT_EQ(placement.width, 495);
    EXPECT_EQ(placement.height, 228);
    EXPECT_EQ(placement.x, 1500 - 495 - 30);
    EXPECT_EQ(placement.y, 950 - 228 - 30);
}

void PopupPlacementClampsToSmallWorkArea() {
    const ReminderPopupWorkArea work{10, 20, 220, 160};

    const ReminderPopupPlacement placement = ComputeReminderPopupPlacement(work, 96);

    EXPECT_EQ(placement.width, 210);
    EXPECT_EQ(placement.height, 140);
    EXPECT_EQ(placement.x, 10);
    EXPECT_EQ(placement.y, 20);
}

const TestCase kTests[] = {
    {"PopupPlacementUsesWorkAreaBottomRight", PopupPlacementUsesWorkAreaBottomRight},
    {"PopupPlacementClampsToSmallWorkArea", PopupPlacementClampsToSmallWorkArea},
};

} // namespace

int main() {
    return RunTests("reminder_popup_policy", kTests);
}
