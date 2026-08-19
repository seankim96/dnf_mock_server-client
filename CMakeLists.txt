cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(DNFMockServer LANGUAGES CXX)

find_package(Boost 1.82 REQUIRED COMPONENTS system)
find_package(flatbuffers CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DNF_FLATBUFFER_SCHEMAS
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/DungeonMessage.fbs
)

flatbuffers_generate_headers(
    TARGET dnf_protocol_generated
    SCHEMAS ${DNF_FLATBUFFER_SCHEMAS}
)

add_library(dnf_core
    src/ChannelManager.cpp
    src/ChannelProtocol.cpp
    src/DungeonAdmissionProtocol.cpp
    src/DungeonCatalog.cpp
    src/DungeonConnectionProtocol.cpp
    src/DungeonInputProcessor.cpp
    src/DungeonInstance.cpp
    src/DungeonLifecycleService.cpp
    src/DungeonManager.cpp
    src/DungeonPlayerState.cpp
    src/DungeonProtocol.cpp
    src/DungeonRoom.cpp
    src/DungeonTickService.cpp
    src/DungeonUdpManager.cpp
    src/DungeonUdpSession.cpp
    src/EnemyCatalog.cpp
    src/LoginValidator.cpp
    src/Packet.cpp
    src/PacketDispatcher.cpp
    src/PartyManager.cpp
    src/ReceiveBuffer.cpp
    src/RoomState.cpp
    src/ServerApplication.cpp
    src/SessionManager.cpp
    src/TcpServer.cpp
    src/TcpSession.cpp
)
target_include_directories(dnf_core PUBLIC include)
target_link_libraries(dnf_core PUBLIC
    Boost::system
    Threads::Threads
    flatbuffers::flatbuffers
    dnf_protocol_generated
)

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

add_executable(channel_manager_test tests/ChannelManagerTest.cpp)
target_link_libraries(channel_manager_test PRIVATE dnf_core)

add_test(NAME channel_manager_test COMMAND channel_manager_test)

add_executable(party_manager_test tests/PartyManagerTest.cpp)
target_link_libraries(party_manager_test PRIVATE dnf_core)

add_test(NAME party_manager_test COMMAND party_manager_test)

add_executable(dungeon_instance_test tests/DungeonInstanceTest.cpp)
target_link_libraries(dungeon_instance_test PRIVATE dnf_core)

add_test(NAME dungeon_instance_test COMMAND dungeon_instance_test)

add_executable(dungeon_manager_test tests/DungeonManagerTest.cpp)
target_link_libraries(dungeon_manager_test PRIVATE dnf_core)

add_test(NAME dungeon_manager_test COMMAND dungeon_manager_test)

add_executable(dungeon_catalog_test tests/DungeonCatalogTest.cpp)
target_link_libraries(dungeon_catalog_test PRIVATE dnf_core)

add_test(NAME dungeon_catalog_test COMMAND dungeon_catalog_test)

add_executable(dungeon_room_test tests/DungeonRoomTest.cpp)
target_link_libraries(dungeon_room_test PRIVATE dnf_core)

add_test(NAME dungeon_room_test COMMAND dungeon_room_test)

add_executable(enemy_catalog_test tests/EnemyCatalogTest.cpp)
target_link_libraries(enemy_catalog_test PRIVATE dnf_core)

add_test(NAME enemy_catalog_test COMMAND enemy_catalog_test)

add_executable(room_state_test tests/RoomStateTest.cpp)
target_link_libraries(room_state_test PRIVATE dnf_core)

add_test(NAME room_state_test COMMAND room_state_test)

add_executable(dungeon_player_state_test tests/DungeonPlayerStateTest.cpp)
target_link_libraries(dungeon_player_state_test PRIVATE dnf_core)

add_test(NAME dungeon_player_state_test COMMAND dungeon_player_state_test)

add_executable(flatbuffer_protocol_test tests/FlatBufferProtocolTest.cpp)
target_link_libraries(flatbuffer_protocol_test PRIVATE dnf_core)

add_test(NAME flatbuffer_protocol_test COMMAND flatbuffer_protocol_test)

add_executable(dungeon_protocol_test tests/DungeonProtocolTest.cpp)
target_link_libraries(dungeon_protocol_test PRIVATE dnf_core)

add_test(NAME dungeon_protocol_test COMMAND dungeon_protocol_test)

add_executable(server_application_test tests/ServerApplicationTest.cpp)
target_link_libraries(server_application_test PRIVATE dnf_core)

add_test(NAME server_application_test COMMAND server_application_test)

add_executable(
    dungeon_admission_protocol_test
    tests/DungeonAdmissionProtocolTest.cpp
)
target_link_libraries(dungeon_admission_protocol_test PRIVATE dnf_core)

add_test(
    NAME dungeon_admission_protocol_test
    COMMAND dungeon_admission_protocol_test
)

add_executable(dungeon_udp_manager_test tests/DungeonUdpManagerTest.cpp)
target_link_libraries(dungeon_udp_manager_test PRIVATE dnf_core)

add_test(NAME dungeon_udp_manager_test COMMAND dungeon_udp_manager_test)

add_executable(
    dungeon_connection_protocol_test
    tests/DungeonConnectionProtocolTest.cpp
)
target_link_libraries(dungeon_connection_protocol_test PRIVATE dnf_core)

add_test(
    NAME dungeon_connection_protocol_test
    COMMAND dungeon_connection_protocol_test
)

add_executable(dungeon_udp_session_test tests/DungeonUdpSessionTest.cpp)
target_link_libraries(dungeon_udp_session_test PRIVATE dnf_core)

add_test(NAME dungeon_udp_session_test COMMAND dungeon_udp_session_test)

add_executable(
    dungeon_input_processor_test
    tests/DungeonInputProcessorTest.cpp
)
target_link_libraries(dungeon_input_processor_test PRIVATE dnf_core)

add_test(
    NAME dungeon_input_processor_test
    COMMAND dungeon_input_processor_test
)

add_executable(
    dungeon_tick_service_test
    tests/DungeonTickServiceTest.cpp
)
target_link_libraries(dungeon_tick_service_test PRIVATE dnf_core)

add_test(
    NAME dungeon_tick_service_test
    COMMAND dungeon_tick_service_test
)

add_executable(
    dungeon_lifecycle_service_test
    tests/DungeonLifecycleServiceTest.cpp
)
target_link_libraries(dungeon_lifecycle_service_test PRIVATE dnf_core)

add_test(
    NAME dungeon_lifecycle_service_test
    COMMAND dungeon_lifecycle_service_test
)
