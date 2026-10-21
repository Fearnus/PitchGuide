# Developer command-line tools. Built only when PITCHNET_BUILD_TOOLS=ON:
#
#   cmake -B build -DPITCHNET_BUILD_TOOLS=ON
#   cmake --build build --target pitchguide_align

# pitchguide-align <reference> <take> [alignment.csv]
# Aligns a take to a reference vocal and writes the frame map as CSV.
add_executable(pitchguide_align ReferenceAlignTool.cpp)
target_link_libraries(pitchguide_align PRIVATE pitchnet_core)
target_compile_features(pitchguide_align PRIVATE cxx_std_17)
set_target_properties(pitchguide_align PROPERTIES OUTPUT_NAME pitchguide-align)
