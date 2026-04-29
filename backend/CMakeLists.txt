cmake_minimum_required(VERSION 3.10)
project(TradingBackend)

set(CMAKE_C_STANDARD 11)

include_directories(include)

add_executable(trading_server
    src/main.c
    src/mongoose.c
    src/stock.c
    src/trading.c
    src/account.c
    src/order_queue.c
)

target_link_libraries(trading_server pthread m)

# Windows (MinGW) also needs ws2_32
if(WIN32)
    target_link_libraries(trading_server ws2_32)
endif()
