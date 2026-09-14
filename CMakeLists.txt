cmake_minimum_required(VERSION 3.21)
project(Outlast2VR LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Outlast 2 VR targets Windows x64.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure with -A x64. Outlast2.exe is 64-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Headers only. We load openxr_loader.dll dynamically at runtime so the mod does not
# link against or proxy the OpenXR loader.
# Bundle the exact P11E SDK headers, avoiding a network fetch on rebuild.
set(openxr_sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr")

add_library(dinput8 SHARED
  src/dinput8_proxy.cpp
  src/dinput8_proxy.def)

target_compile_definitions(dinput8 PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN DIRECTINPUT_VERSION=0x0800
  XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11 XR_NO_PROTOTYPES)
target_include_directories(dinput8 PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_link_libraries(dinput8 PRIVATE d3d11 d3dcompiler dxgi advapi32 user32 windowscodecs ole32 winmm)
set_target_properties(dinput8 PROPERTIES OUTPUT_NAME "dinput8")

enable_testing()
set(_csc "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
if(EXISTS "${_csc}")
  file(TO_NATIVE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" _native_bin)
  add_custom_command(OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Outlast2VR.exe"
    COMMAND "${_csc}" /nologo /target:winexe /platform:x64
      "/out:${_native_bin}\\Outlast2VR.exe"
      /reference:System.Windows.Forms.dll /reference:System.Drawing.dll
      /resource:Outlast2VR_logo.png,Outlast2VR.logo.png
      /resource:Outlast2VR_launcher_background.png,Outlast2VR.launcher.background.png
      /resource:QuestControllers_CL4.png,Outlast2VR.quest.controllers.png
      Outlast2VR_Launcher.cs VrSettings.cs
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/Outlast2VR_intro.mp3"
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Outlast2VR_intro.mp3"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS Outlast2VR_Launcher.cs VrSettings.cs Outlast2VR_logo.png Outlast2VR_launcher_background.png QuestControllers_CL4.png assets/Outlast2VR_intro.mp3 VERBATIM)
  add_custom_target(vr_launcher ALL DEPENDS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Outlast2VR.exe")
  add_custom_command(OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VrSettingsTests.exe"
    COMMAND "${_csc}" /nologo /target:exe "/out:${_native_bin}\\VrSettingsTests.exe"
      VrSettings.cs "tests\\VrSettingsTests.cs"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS VrSettings.cs tests/VrSettingsTests.cs VERBATIM)
  add_custom_target(vr_settings_tests ALL DEPENDS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VrSettingsTests.exe")
  add_test(NAME vr_launcher_settings_backup_and_preservation COMMAND "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/VrSettingsTests.exe")
endif()
add_executable(p46_visual_tests tests/p46_visual_tests.cpp)
target_include_directories(p46_visual_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_definitions(p46_visual_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(p46_visual_tests PRIVATE /EHsc)
target_link_libraries(p46_visual_tests PRIVATE d3d11 d3dcompiler dxgi advapi32 user32 windowscodecs ole32 winmm)
add_test(NAME p46_world_screen_and_visual_regressions COMMAND p46_visual_tests)
add_executable(p12_projection_tests tests/p12_projection_tests.cpp)
target_include_directories(p12_projection_tests PRIVATE src)
target_compile_definitions(p12_projection_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(p12_projection_tests PRIVATE d3d11 d3dcompiler)
target_compile_options(p12_projection_tests PRIVATE /EHsc)
add_test(NAME p12_projection_cpu_gpu COMMAND p12_projection_tests)

add_executable(p12_aer_tests tests/p12_aer_tests.cpp)
target_compile_definitions(p12_aer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p12_aer_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p12_aer_tests PRIVATE /EHsc)
target_link_libraries(p12_aer_tests PRIVATE d3d11 dxgi advapi32 user32 winmm)
add_test(NAME p12_aer_history COMMAND p12_aer_tests)

add_executable(p13_ui_tests tests/p13_ui_tests.cpp)
target_compile_definitions(p13_ui_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p13_ui_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p13_ui_tests PRIVATE /EHsc)
target_link_libraries(p13_ui_tests PRIVATE d3d11 d3dcompiler dxgi advapi32 user32 winmm)
add_test(NAME p13_ui_and_timing COMMAND p13_ui_tests)
add_executable(p17_capture_tests tests/p17_capture_tests.cpp)
target_include_directories(p17_capture_tests PRIVATE src)
target_compile_options(p17_capture_tests PRIVATE /EHsc)
add_test(NAME p17_world_camera COMMAND p17_capture_tests)
add_executable(p18_input_tests tests/p18_input_tests.cpp)
target_compile_definitions(p18_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p18_input_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p18_input_tests PRIVATE /EHsc)
target_link_libraries(p18_input_tests PRIVATE d3d11 dxgi advapi32 user32 winmm)
add_test(NAME p18_quest_input COMMAND p18_input_tests)
add_executable(p19_rig_tests tests/p19_rig_tests.cpp)
target_compile_definitions(p19_rig_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p19_rig_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p19_rig_tests PRIVATE /EHsc)
target_link_libraries(p19_rig_tests PRIVATE d3d11 dxgi advapi32 user32 winmm)
add_test(NAME p19_rig_and_poses COMMAND p19_rig_tests)
add_executable(p20_ik_tests tests/p20_ik_tests.cpp)
target_compile_definitions(p20_ik_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p20_ik_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p20_ik_tests PRIVATE /EHsc)
target_link_libraries(p20_ik_tests PRIVATE d3d11 dxgi advapi32 user32 winmm)
add_test(NAME p20_body_ik COMMAND p20_ik_tests)
add_executable(p22_body_anchor_tests tests/p22_body_anchor_tests.cpp)
target_include_directories(p22_body_anchor_tests PRIVATE src)
target_compile_options(p22_body_anchor_tests PRIVATE /EHsc)
add_test(NAME p22_body_anchor COMMAND p22_body_anchor_tests)
add_executable(p41_physical_tests tests/p41_physical_tests.cpp)
target_include_directories(p41_physical_tests PRIVATE src)
target_compile_options(p41_physical_tests PRIVATE /EHsc)
add_test(NAME p41_physical_interactions_and_collision COMMAND p41_physical_tests)
add_executable(p25_holster_tests tests/p25_holster_tests.cpp)
target_compile_definitions(p25_holster_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p25_holster_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p25_holster_tests PRIVATE /EHsc)
target_link_libraries(p25_holster_tests PRIVATE d3d11 dxgi advapi32 user32 winmm)
add_test(NAME p25_native_holster COMMAND p25_holster_tests)



add_executable(p26_grip_tests tests/p26_grip_tests.cpp)
target_compile_options(p26_grip_tests PRIVATE /EHsc)
add_test(NAME p26_grip_and_reach COMMAND p26_grip_tests)

add_executable(p27_native_hud_tests tests/p27_native_hud_tests.cpp)
add_test(NAME p27_native_hud_signature COMMAND p27_native_hud_tests)

add_executable(p28_right_hand_tests tests/p28_right_hand_tests.cpp)
add_test(NAME p28_right_hand_axes COMMAND p28_right_hand_tests)

add_executable(p29_camcorder_target_tests tests/p29_camcorder_target_tests.cpp)
target_include_directories(p29_camcorder_target_tests PRIVATE src)
add_test(NAME p29_camcorder_target_probe COMMAND p29_camcorder_target_tests)

add_executable(p31_capture_tests tests/p31_capture_tests.cpp)
target_compile_definitions(p31_capture_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p31_capture_tests PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_options(p31_capture_tests PRIVATE /EHsc)
target_link_libraries(p31_capture_tests PRIVATE d3d11 d3dcompiler dxgi advapi32 user32 winmm)
add_test(NAME p31_lens_pose COMMAND p31_capture_tests)


add_executable(p32_cleanup_tests tests/p32_cleanup_tests.cpp)
target_include_directories(p32_cleanup_tests PRIVATE src)
target_compile_options(p32_cleanup_tests PRIVATE /EHsc)
add_test(NAME p32_cleanup_and_mirror COMMAND p32_cleanup_tests)


add_executable(p33_bandage_tests tests/p33_bandage_tests.cpp)
target_include_directories(p33_bandage_tests PRIVATE src)
target_compile_options(p33_bandage_tests PRIVATE /EHsc)
add_test(NAME p33_physical_bandage_and_asset_parser COMMAND p33_bandage_tests)

# P33_D3DCOMPILER_TEST_LINK_FIX_V2
# P33EnsureRenderer() is present in legacy test translation units and calls D3DCompile.
foreach(_p33_test IN ITEMS
    p12_aer_tests
    p13_ui_tests
    p18_input_tests
    p19_rig_tests
    p20_ik_tests
    p25_holster_tests
    p31_capture_tests
)
  if(TARGET ${_p33_test})
    target_link_libraries(${_p33_test} PRIVATE d3dcompiler)
  endif()
endforeach()
