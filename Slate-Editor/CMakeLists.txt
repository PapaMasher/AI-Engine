add_executable(Slate-Editor main.cpp)
target_link_libraries(Slate-Editor PRIVATE Slate)
set_target_properties(Slate-Editor PROPERTIES FOLDER "Slate")

add_dependencies(Slate-Editor SlateAssets)
