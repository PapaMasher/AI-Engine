add_executable(Slate-Runtime main.cpp)
target_link_libraries(Slate-Runtime PRIVATE Slate)
set_target_properties(Slate-Runtime PROPERTIES FOLDER "Slate")

add_dependencies(Slate-Runtime SlateAssets)
