#include "test_vk_keys.h"
#include "test_vk_manifest.h"
#include "test_vk_gameplay.h"

int main() {
    printf("=== Galaxy-Android Vulkan Shader Inventory & Regression Test Suite ===\n");
    test_key_round_trip();
    test_placeholder_exclusion();
    test_atomic_manifest_validation();
    test_quicksave_starbit_accounting();
    test_copy_texture_slot_telemetry();
    test_frame_pacer_timing();
    printf("=== ALL C++ REGRESSION TESTS PASSED! ===\n");
    return 0;
}
