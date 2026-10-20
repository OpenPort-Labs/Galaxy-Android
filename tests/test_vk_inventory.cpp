#include "test_vk_keys.h"
#include "test_vk_manifest.h"
#include "test_vk_gameplay.h"
#include "test_vk_hot_path.h"
#include "test_manifest_roundtrip.h"

int main() {
    printf("=== Galaxy-Android Vulkan Shader Inventory & Regression Test Suite ===\n");
    test_key_round_trip();
    test_placeholder_exclusion();
    test_shader_inventory_concurrency();
    test_pipeline_mru_production();
    test_tex_mask_iteration_production();
    test_draw_descriptor_cache_production();
    test_atomic_manifest_validation();
    test_manifest_schema_roundtrip();
    test_quicksave_starbit_accounting();
    test_copy_texture_slot_telemetry();
    test_frame_pacer_timing();
    printf("=== ALL C++ REGRESSION TESTS PASSED! ===\n");
    return 0;
}
