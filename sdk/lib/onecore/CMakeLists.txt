
list(APPEND SOURCE onecore.cpp)

add_library(onecore ${SOURCE})
add_dependencies(onecore psdk)
