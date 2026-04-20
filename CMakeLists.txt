cmake_minimum_required(VERSION 3.14)
project(liquibook_tests CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS unit_test_framework)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(liquibook_unit_tests
    src/simple/simple_order.cpp
    test/unit/ut_main.cpp
    test/unit/ut_order_book.cpp
    test/unit/ut_bbo_order_book.cpp
    test/unit/ut_depth.cpp
    test/unit/ut_immediate_or_cancel.cpp
    test/unit/ut_all_or_none.cpp
    test/unit/ut_stop_orders.cpp
    test/unit/ut_market_price.cpp
    test/unit/ut_order_book_shared_ptr.cpp
    test/unit/ut_iceberg_orders.cpp
)

target_link_libraries(liquibook_unit_tests Boost::unit_test_framework)
target_compile_definitions(liquibook_unit_tests PRIVATE BOOST_TEST_DYN_LINK)
