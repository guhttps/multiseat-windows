cmake_minimum_required(VERSION 3.24)
project(multiseat_windows VERSION 0.0.1 LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "These experiments require Windows x64, MSVC and the Windows SDK.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure the Windows x64 target with -A x64.")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

function(multiseat_experiment target source)
  add_executable(${target} ${source})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /sdl /guard:cf)
  target_link_options(${target} PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
endfunction()

multiseat_experiment(ms-diagnostic experiments/001-environment/main.cpp)
target_link_libraries(ms-diagnostic PRIVATE user32)
multiseat_experiment(ms-keyboard-probe experiments/002-keyboard-probe/main.cpp)
target_link_libraries(ms-keyboard-probe PRIVATE user32)
multiseat_experiment(ms-desktop-probe experiments/003-desktop-probe/main.cpp)
target_link_libraries(ms-desktop-probe PRIVATE user32)
multiseat_experiment(ms-session-probe experiments/004-session-probe/main.cpp)
target_link_libraries(ms-session-probe PRIVATE user32 wtsapi32)
multiseat_experiment(ms-seat-demo experiments/005-seat-demo/main.cpp)
target_link_libraries(ms-seat-demo PRIVATE user32)
multiseat_experiment(ms-child-session-probe experiments/006-child-session/main.cpp)
target_link_libraries(ms-child-session-probe PRIVATE wtsapi32 ole32 oleaut32)

# Build the .NET Framework host into the same configuration directory as the
# native probes. This does not enable child sessions or make any connection.
option(MULTISEAT_BUILD_CHILD_SESSION_HOST "Build the .NET Framework child-session experiment" ON)
if(MULTISEAT_BUILD_CHILD_SESSION_HOST)
  set(child_session_compiler "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
  if(NOT EXISTS "${child_session_compiler}")
    message(FATAL_ERROR ".NET Framework x64 compiler missing; install .NET Framework or set MULTISEAT_BUILD_CHILD_SESSION_HOST=OFF.")
  endif()
  set(child_session_executable "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ms-child-session-test.exe")
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/experiments/006-child-session/ChildSessionTest.cs" child_session_source_native)
  file(TO_NATIVE_PATH "${child_session_executable}" child_session_executable_native)
  add_custom_command(
    OUTPUT "${child_session_executable}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E env --unset=LIB "${child_session_compiler}"
      /nologo /target:exe /platform:x64 /warn:4 /warnaserror+
      "/out:${child_session_executable_native}" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll
      "${child_session_source_native}"
    DEPENDS experiments/006-child-session/ChildSessionTest.cs
    VERBATIM)
  add_custom_target(ms-child-session-test ALL DEPENDS "${child_session_executable}")
endif()

set(control_panel_compiler "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
if(NOT EXISTS "${control_panel_compiler}")
  message(FATAL_ERROR ".NET Framework x64 compiler is required to build the Windows control panel.")
endif()
set(control_panel_executable "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/MultiseatPrototype.exe")
file(TO_NATIVE_PATH "${control_panel_executable}" control_panel_executable_native)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app/app.manifest" control_panel_manifest_native)
set(control_panel_sources app/Program.cs app/Configuration.cs app/Devices.cs app/MainForm.cs)
set(control_panel_sources_native)
foreach(control_panel_source IN LISTS control_panel_sources)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${control_panel_source}" control_panel_source_native)
  list(APPEND control_panel_sources_native "${control_panel_source_native}")
endforeach()
add_custom_command(
  OUTPUT "${control_panel_executable}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LIB "${control_panel_compiler}"
    /nologo /target:winexe /platform:x64 /warn:4 /warnaserror+ /debug- /optimize+ /codepage:65001
    "/out:${control_panel_executable_native}" "/win32manifest:${control_panel_manifest_native}"
    /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Runtime.Serialization.dll
    ${control_panel_sources_native}
  DEPENDS ${control_panel_sources} app/app.manifest
  VERBATIM)
add_custom_target(ms-control-panel ALL DEPENDS "${control_panel_executable}")

if(BUILD_TESTING)
  multiseat_experiment(ms-raw-input-tests experiments/tests/raw-input.cpp)
  add_test(NAME raw-input-validation COMMAND ms-raw-input-tests)
  add_test(NAME environment-read-only COMMAND ms-diagnostic)
  add_test(NAME keyboard-window-smoke COMMAND ms-keyboard-probe --smoke)
  add_test(NAME session-read-only COMMAND ms-session-probe)
  add_test(NAME own-panel-routing COMMAND ms-seat-demo --self-test)
  add_test(NAME own-panel-window-smoke COMMAND ms-seat-demo --smoke)
  add_test(NAME child-capability-read-only COMMAND ms-child-session-probe)
  add_test(NAME control-panel-logic COMMAND "${control_panel_executable}" --self-test)
  add_test(NAME control-panel-smoke COMMAND "${control_panel_executable}" --smoke)
  if(MULTISEAT_BUILD_CHILD_SESSION_HOST)
    add_test(NAME child-observation-logic COMMAND "${child_session_executable}" --self-test)
    add_test(NAME child-control-smoke COMMAND "${child_session_executable}" --smoke)
  endif()
  get_property(multiseat_tests DIRECTORY PROPERTY TESTS)
  set_tests_properties(${multiseat_tests} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

# Compile the guest observer in the default build so the C# security analysis
# sees it too. Its only automatic execution is an offline self-test; launching
# Notepad/observing a guest session requires the separate explicit lab mode.
set(rds_observer_executable "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/RdsLabObserver.exe")
file(TO_NATIVE_PATH "${rds_observer_executable}" rds_observer_executable_native)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/experiments/008-rds-client/guest-observer/RdsLabObserver.cs" rds_observer_source_native)
add_custom_command(
  OUTPUT "${rds_observer_executable}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LIB "${control_panel_compiler}"
    /nologo /target:winexe /platform:x64 /warn:4 /warnaserror+ /debug- /optimize+ /codepage:65001
    "/out:${rds_observer_executable_native}"
    /reference:System.Management.dll /reference:System.Runtime.Serialization.dll /reference:System.Security.dll
    "${rds_observer_source_native}"
  DEPENDS experiments/008-rds-client/guest-observer/RdsLabObserver.cs
  VERBATIM)
add_custom_target(ms-rds-guest-observer ALL DEPENDS "${rds_observer_executable}")
if(BUILD_TESTING)
  add_test(NAME rds-guest-observer-offline COMMAND "${rds_observer_executable}" --self-test)
  set_tests_properties(rds-guest-observer-offline PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

# Optional Windows Server VM laboratory only. This is an RDP transport client,
# not a native Windows-client multiseat engine. Dependencies are provisioned
# explicitly outside this source tree; configuring the default build downloads
# nothing and cannot create or connect any RDP session.
option(MULTISEAT_BUILD_RDS_LAB "Build the optional two-context FreeRDP Server laboratory" OFF)
if(MULTISEAT_BUILD_RDS_LAB)
  find_package(FreeRDP 3.32.2 EXACT CONFIG REQUIRED)
  find_package(WinPR 3.32.2 EXACT CONFIG REQUIRED)
  set(MULTISEAT_RDS_RUNTIME_DIR "" CACHE PATH "Private SDK bin directory for the RDS lab CTest process PATH")
  set(rds_lab_root "${CMAKE_CURRENT_SOURCE_DIR}/experiments/008-rds-client")
  multiseat_experiment(ms-rds-lab-client "${rds_lab_root}/main.cpp")
  target_sources(ms-rds-lab-client PRIVATE
    "${rds_lab_root}/config.cpp" "${rds_lab_root}/input-router.cpp"
    "${rds_lab_root}/transport.cpp" "${rds_lab_root}/network.cpp"
    "${rds_lab_root}/ssl-diagnostics.cpp" "${rds_lab_root}/tests.cpp")
  target_link_libraries(ms-rds-lab-client PRIVATE freerdp winpr
    user32 gdi32 crypt32 bcrypt ncrypt runtimeobject shell32 ole32 advapi32 ws2_32)
  if(BUILD_TESTING)
    multiseat_experiment(ms-rds-lab-logic "${rds_lab_root}/tests-main.cpp")
    target_sources(ms-rds-lab-logic PRIVATE "${rds_lab_root}/config.cpp"
      "${rds_lab_root}/input-router.cpp" "${rds_lab_root}/network.cpp"
      "${rds_lab_root}/ssl-diagnostics.cpp" "${rds_lab_root}/tests.cpp")
    target_link_libraries(ms-rds-lab-logic PRIVATE user32 crypt32 bcrypt ncrypt runtimeobject shell32 ole32 advapi32 ws2_32)
    add_test(NAME rds-lab-offline-policy COMMAND ms-rds-lab-logic)
    add_test(NAME rds-lab-window-smoke COMMAND ms-rds-lab-client --smoke)
    set_tests_properties(rds-lab-offline-policy rds-lab-window-smoke
      PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    if(MULTISEAT_RDS_RUNTIME_DIR)
      set_tests_properties(rds-lab-window-smoke PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MULTISEAT_RDS_RUNTIME_DIR}")
    endif()
  endif()
endif()
