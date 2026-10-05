# 1. Definir a versão mínima do CMake
cmake_minimum_required(VERSION 3.10)

# 2. Nome do projeto
project(InspectionEdgeNode VERSION 1.0)

# 3. Configurar C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. Incluir as pastas de cabeçalho
include_directories(src/Core src/HAL src/Mocks src/Survey)

# --- CONFIGURAÇÃO DOS TESTES (GTest) ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Executável de Testes
add_executable(RunTests
    tests/main_test.cpp
    src/Core/EdgeProcessor.cpp
    tests/survey_test.cpp
    src/Survey/Survey.cpp
)
target_link_libraries(RunTests GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(RunTests)

add_executable(SimulateSystem
    src/main_simulation.cpp
    src/Core/EdgeProcessor.cpp
)

add_executable(SurveySimulation
    src/survey_simulation.cpp
    src/Survey/Survey.cpp
)
