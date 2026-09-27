# CMake generated Testfile for 
# Source directory: D:/ESPProject/guangheng_energy_companion/tests
# Build directory: D:/ESPProject/guangheng_energy_companion/build-host
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(gh_model_tests "D:/ESPProject/guangheng_energy_companion/build-host/Debug/gh_model_tests.exe")
  set_tests_properties(gh_model_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;21;add_test;D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(gh_model_tests "D:/ESPProject/guangheng_energy_companion/build-host/Release/gh_model_tests.exe")
  set_tests_properties(gh_model_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;21;add_test;D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(gh_model_tests "D:/ESPProject/guangheng_energy_companion/build-host/MinSizeRel/gh_model_tests.exe")
  set_tests_properties(gh_model_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;21;add_test;D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(gh_model_tests "D:/ESPProject/guangheng_energy_companion/build-host/RelWithDebInfo/gh_model_tests.exe")
  set_tests_properties(gh_model_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;21;add_test;D:/ESPProject/guangheng_energy_companion/tests/CMakeLists.txt;0;")
else()
  add_test(gh_model_tests NOT_AVAILABLE)
endif()
