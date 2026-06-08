
add_definitions(-D_SHLWAPI_ -D_SHELL32_)

list(APPEND SOURCE onecore.cpp)

add_library(onecore ${SOURCE})
add_dependencies(onecore psdk)
