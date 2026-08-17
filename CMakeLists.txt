cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(DNFMockServer LANGUAGES CXX)

find_package(Boost 1.82 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(dnf_core
    src/LoginValidator.cpp
    src/Packet.cpp
    src/PacketDispatcher.cpp
    src/ReceiveBuffer.cpp
    src/ServerApplication.cpp
    src/SessionManager.cpp
    src/TcpServer.cpp
    src/TcpSession.cpp
)
target_include_directories(dnf_core PUBLIC include)
target_link_libraries(dnf_core PUBLIC Boost::system Threads::Threads)

if(WIN32)
    target_compile_definitions(dnf_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(dnf_core PUBLIC ws2_32 mswsock)
endif()

add_executable(dnf_mock_server src/main.cpp)
target_link_libraries(dnf_mock_server PRIVATE dnf_core)

add_executable(dnf_mock_client
    src/mock_client_main.cpp
    src/TcpClient.cpp
)
target_include_directories(dnf_mock_client PRIVATE include)
target_link_libraries(dnf_mock_client PRIVATE dnf_core)

enable_testing()

add_executable(receive_buffer_test tests/ReceiveBufferTest.cpp)
target_link_libraries(receive_buffer_test PRIVATE dnf_core)

add_test(NAME receive_buffer_test COMMAND receive_buffer_test)

add_executable(packet_dispatcher_test tests/PacketDispatcherTest.cpp)
target_link_libraries(packet_dispatcher_test PRIVATE dnf_core)

add_test(NAME packet_dispatcher_test COMMAND packet_dispatcher_test)

add_executable(login_validator_test tests/LoginValidatorTest.cpp)
target_link_libraries(login_validator_test PRIVATE dnf_core)

add_test(NAME login_validator_test COMMAND login_validator_test)
