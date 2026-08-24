cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(DNFMockServer LANGUAGES CXX)

include(cmake/ProjectOptions.cmake)
dnf_configure_project_options()

find_package(Boost 1.82 REQUIRED COMPONENTS system)
find_package(flatbuffers CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DNF_FLATBUFFER_SCHEMAS
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/AuthMessage.fbs
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/DungeonMessage.fbs
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/TcpMessage.fbs
)

flatbuffers_generate_headers(
    TARGET dnf_protocol_generated
    SCHEMAS ${DNF_FLATBUFFER_SCHEMAS}
)

add_library(dnf_core
    src/Account.cpp
    src/AccountAuthenticationService.cpp
    src/AccountProvisioningService.cpp
    src/AuthFlatBufferCodec.cpp
    src/AuthPacketDispatcher.cpp
    src/AuthServerApplication.cpp
    src/AuthServerSessionState.cpp
    src/AuthTicketIssuer.cpp
    src/AuthTicketVerifier.cpp
    src/AuthTlsServer.cpp
    src/AuthTlsSession.cpp
    src/ChannelManager.cpp
    src/ChannelProtocol.cpp
    src/CharacterListService.cpp
    src/CharacterSelectionService.cpp
    src/DatabaseExecutor.cpp
    src/DungeonCombatProcessor.cpp
    src/DungeonAdmissionProtocol.cpp
    src/DungeonCatalog.cpp
    src/DungeonCatalogProtocol.cpp
    src/DungeonConnectionProtocol.cpp
    src/DungeonStaticDataProtocol.cpp
    src/DungeonMovementProcessor.cpp
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
    src/GameDataLoader.cpp
    src/LoginProtocol.cpp
    src/LoginAttemptLimiter.cpp
    src/LoginValidator.cpp
    src/Packet.cpp
    src/PacketDispatcher.cpp
    src/PartyManager.cpp
    src/PartyProtocol.cpp
    src/PlayerLoginService.cpp
    src/PlayerProfile.cpp
    src/PlayerSkillBook.cpp
    src/ReceiveBuffer.cpp
    src/RoomState.cpp
    src/ScryptPasswordHasher.cpp
    src/ServerApplication.cpp
    src/SessionAuthState.cpp
    src/SessionManager.cpp
    src/SkillCatalog.cpp
    src/SqliteAccountPlayerRepository.cpp
    src/SqliteAccountRepository.cpp
    src/SqliteAuthTicketStore.cpp
    src/SqliteAuthTicketVerifier.cpp
    src/SqliteCharacterProvisioner.cpp
    src/SqliteDatabase.cpp
    src/SqlitePlayerRepository.cpp
    src/TcpFlatBufferCodec.cpp
    src/TcpServer.cpp
    src/TcpSession.cpp
)
target_include_directories(dnf_core PUBLIC include)
target_link_libraries(dnf_core PUBLIC
    dnf_project_options
    Boost::system
    OpenSSL::Crypto
    OpenSSL::SSL
    SQLite3::SQLite3
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

add_executable(dnf_auth_server src/auth_main.cpp)
target_link_libraries(dnf_auth_server PRIVATE dnf_core)

add_executable(dnf_admin src/admin_main.cpp)
target_link_libraries(dnf_admin PRIVATE dnf_core)

enable_testing()

add_test(NAME admin_help_test COMMAND dnf_admin --help)
set_tests_properties(
    admin_help_test
    PROPERTIES PASS_REGULAR_EXPRESSION "create-character"
)

add_test(
    NAME admin_invalid_account_id_test
    COMMAND dnf_admin create-character unused.db invalid Player_1
)
set_tests_properties(
    admin_invalid_account_id_test
    PROPERTIES WILL_FAIL TRUE
)

add_test(NAME auth_server_help_test COMMAND dnf_auth_server --help)
set_tests_properties(
    auth_server_help_test
    PROPERTIES PASS_REGULAR_EXPRESSION "Usage:"
)

add_test(
    NAME auth_server_invalid_port_test
    COMMAND dnf_auth_server invalid unused.db cert.pem key.pem localhost 7777
)
set_tests_properties(
    auth_server_invalid_port_test
    PROPERTIES WILL_FAIL TRUE
)

add_executable(
    dev_auth_ticket_verifier_test
    tests/DevAuthTicketVerifier.cpp
    tests/DevAuthTicketVerifierTest.cpp
)
target_link_libraries(dev_auth_ticket_verifier_test PRIVATE dnf_core)

add_test(
    NAME dev_auth_ticket_verifier_test
    COMMAND dev_auth_ticket_verifier_test
)

add_executable(auth_ticket_issuer_test tests/AuthTicketIssuerTest.cpp)
target_link_libraries(auth_ticket_issuer_test PRIVATE dnf_core)

add_test(NAME auth_ticket_issuer_test COMMAND auth_ticket_issuer_test)

add_executable(
    account_authentication_service_test
    tests/AccountAuthenticationServiceTest.cpp
)
target_link_libraries(
    account_authentication_service_test
    PRIVATE dnf_core
)

add_test(
    NAME account_authentication_service_test
    COMMAND account_authentication_service_test
)

add_executable(
    account_provisioning_service_test
    tests/AccountProvisioningServiceTest.cpp
)
target_link_libraries(
    account_provisioning_service_test
    PRIVATE dnf_core
)

add_test(
    NAME account_provisioning_service_test
    COMMAND account_provisioning_service_test
)

add_executable(
    sqlite_character_provisioner_test
    tests/SqliteCharacterProvisionerTest.cpp
)
target_link_libraries(
    sqlite_character_provisioner_test
    PRIVATE dnf_core
)

add_test(
    NAME sqlite_character_provisioner_test
    COMMAND sqlite_character_provisioner_test
)

add_executable(
    character_selection_service_test
    tests/CharacterSelectionServiceTest.cpp
)
target_link_libraries(
    character_selection_service_test
    PRIVATE dnf_core
)

add_test(
    NAME character_selection_service_test
    COMMAND character_selection_service_test
)

add_executable(
    character_list_service_test
    tests/CharacterListServiceTest.cpp
)
target_link_libraries(
    character_list_service_test
    PRIVATE dnf_core
)

add_test(
    NAME character_list_service_test
    COMMAND character_list_service_test
)

add_executable(
    scrypt_password_hasher_test
    tests/ScryptPasswordHasherTest.cpp
)
target_link_libraries(scrypt_password_hasher_test PRIVATE dnf_core)

add_test(
    NAME scrypt_password_hasher_test
    COMMAND scrypt_password_hasher_test
)

add_executable(database_executor_test tests/DatabaseExecutorTest.cpp)
target_link_libraries(database_executor_test PRIVATE dnf_core)

add_test(NAME database_executor_test COMMAND database_executor_test)

add_executable(
    database_executor_backpressure_test
    tests/DatabaseExecutorBackpressureTest.cpp
)
target_link_libraries(database_executor_backpressure_test PRIVATE dnf_core)

add_test(
    NAME database_executor_backpressure_test
    COMMAND database_executor_backpressure_test
)

add_executable(
    login_attempt_limiter_test
    tests/LoginAttemptLimiterTest.cpp
)
target_link_libraries(login_attempt_limiter_test PRIVATE dnf_core)

add_test(
    NAME login_attempt_limiter_test
    COMMAND login_attempt_limiter_test
)

add_executable(
    async_service_backpressure_test
    tests/AsyncServiceBackpressureTest.cpp
)
target_link_libraries(async_service_backpressure_test PRIVATE dnf_core)

add_test(
    NAME async_service_backpressure_test
    COMMAND async_service_backpressure_test
)

add_executable(game_data_loader_test tests/GameDataLoaderTest.cpp)
target_link_libraries(game_data_loader_test PRIVATE dnf_core)

add_test(NAME game_data_loader_test COMMAND game_data_loader_test)

add_executable(receive_buffer_test tests/ReceiveBufferTest.cpp)
target_link_libraries(receive_buffer_test PRIVATE dnf_core)

add_test(NAME receive_buffer_test COMMAND receive_buffer_test)

add_executable(session_auth_state_test tests/SessionAuthStateTest.cpp)
target_link_libraries(session_auth_state_test PRIVATE dnf_core)

add_test(NAME session_auth_state_test COMMAND session_auth_state_test)

add_executable(
    auth_server_session_state_test
    tests/AuthServerSessionStateTest.cpp
)
target_link_libraries(auth_server_session_state_test PRIVATE dnf_core)

add_test(
    NAME auth_server_session_state_test
    COMMAND auth_server_session_state_test
)

add_executable(
    packet_dispatcher_test
    tests/DevAuthTicketVerifier.cpp
    tests/PacketDispatcherTest.cpp
)
target_link_libraries(packet_dispatcher_test PRIVATE dnf_core)

add_test(NAME packet_dispatcher_test COMMAND packet_dispatcher_test)

add_executable(login_validator_test tests/LoginValidatorTest.cpp)
target_link_libraries(login_validator_test PRIVATE dnf_core)

add_test(NAME login_validator_test COMMAND login_validator_test)

add_executable(login_protocol_test tests/LoginProtocolTest.cpp)
target_link_libraries(login_protocol_test PRIVATE dnf_core)

add_test(NAME login_protocol_test COMMAND login_protocol_test)

add_executable(channel_manager_test tests/ChannelManagerTest.cpp)
target_link_libraries(channel_manager_test PRIVATE dnf_core)

add_test(NAME channel_manager_test COMMAND channel_manager_test)

add_executable(party_manager_test tests/PartyManagerTest.cpp)
target_link_libraries(party_manager_test PRIVATE dnf_core)

add_test(NAME party_manager_test COMMAND party_manager_test)

add_executable(party_protocol_test tests/PartyProtocolTest.cpp)
target_link_libraries(party_protocol_test PRIVATE dnf_core)

add_test(NAME party_protocol_test COMMAND party_protocol_test)

add_executable(dungeon_instance_test tests/DungeonInstanceTest.cpp)
target_link_libraries(dungeon_instance_test PRIVATE dnf_core)

add_test(NAME dungeon_instance_test COMMAND dungeon_instance_test)

add_executable(dungeon_manager_test tests/DungeonManagerTest.cpp)
target_link_libraries(dungeon_manager_test PRIVATE dnf_core)

add_test(NAME dungeon_manager_test COMMAND dungeon_manager_test)

add_executable(dungeon_catalog_test tests/DungeonCatalogTest.cpp)
target_link_libraries(dungeon_catalog_test PRIVATE dnf_core)

add_test(NAME dungeon_catalog_test COMMAND dungeon_catalog_test)

add_executable(
    dungeon_catalog_protocol_test
    tests/DungeonCatalogProtocolTest.cpp
)
target_link_libraries(dungeon_catalog_protocol_test PRIVATE dnf_core)

add_test(
    NAME dungeon_catalog_protocol_test
    COMMAND dungeon_catalog_protocol_test
)

add_executable(dungeon_room_test tests/DungeonRoomTest.cpp)
target_link_libraries(dungeon_room_test PRIVATE dnf_core)

add_test(NAME dungeon_room_test COMMAND dungeon_room_test)

add_executable(enemy_catalog_test tests/EnemyCatalogTest.cpp)
target_link_libraries(enemy_catalog_test PRIVATE dnf_core)

add_test(NAME enemy_catalog_test COMMAND enemy_catalog_test)

add_executable(skill_catalog_test tests/SkillCatalogTest.cpp)
target_link_libraries(skill_catalog_test PRIVATE dnf_core)

add_test(NAME skill_catalog_test COMMAND skill_catalog_test)

add_executable(player_skill_book_test tests/PlayerSkillBookTest.cpp)
target_link_libraries(player_skill_book_test PRIVATE dnf_core)

add_test(NAME player_skill_book_test COMMAND player_skill_book_test)

add_executable(player_profile_test tests/PlayerProfileTest.cpp)
target_link_libraries(player_profile_test PRIVATE dnf_core)

add_test(NAME player_profile_test COMMAND player_profile_test)

add_executable(
    player_login_service_test
    tests/DevAuthTicketVerifier.cpp
    tests/PlayerLoginServiceTest.cpp
)
target_link_libraries(player_login_service_test PRIVATE dnf_core)

add_test(
    NAME player_login_service_test
    COMMAND player_login_service_test
)

add_executable(sqlite_database_test tests/SqliteDatabaseTest.cpp)
target_link_libraries(sqlite_database_test PRIVATE dnf_core)

add_test(NAME sqlite_database_test COMMAND sqlite_database_test)

add_executable(
    sqlite_account_repository_test
    tests/SqliteAccountRepositoryTest.cpp
)
target_link_libraries(sqlite_account_repository_test PRIVATE dnf_core)

add_test(
    NAME sqlite_account_repository_test
    COMMAND sqlite_account_repository_test
)

add_executable(
    sqlite_account_player_repository_test
    tests/SqliteAccountPlayerRepositoryTest.cpp
)
target_link_libraries(
    sqlite_account_player_repository_test
    PRIVATE dnf_core
)

add_test(
    NAME sqlite_account_player_repository_test
    COMMAND sqlite_account_player_repository_test
)

add_executable(
    sqlite_auth_ticket_verifier_test
    tests/SqliteAuthTicketVerifierTest.cpp
)
target_link_libraries(sqlite_auth_ticket_verifier_test PRIVATE dnf_core)

add_test(
    NAME sqlite_auth_ticket_verifier_test
    COMMAND sqlite_auth_ticket_verifier_test
)

add_executable(
    sqlite_player_repository_test
    tests/SqlitePlayerRepositoryTest.cpp
)
target_link_libraries(sqlite_player_repository_test PRIVATE dnf_core)

add_test(
    NAME sqlite_player_repository_test
    COMMAND sqlite_player_repository_test
)

add_executable(room_state_test tests/RoomStateTest.cpp)
target_link_libraries(room_state_test PRIVATE dnf_core)

add_test(NAME room_state_test COMMAND room_state_test)

add_executable(dungeon_player_state_test tests/DungeonPlayerStateTest.cpp)
target_link_libraries(dungeon_player_state_test PRIVATE dnf_core)

add_test(NAME dungeon_player_state_test COMMAND dungeon_player_state_test)

add_executable(flatbuffer_protocol_test tests/FlatBufferProtocolTest.cpp)
target_link_libraries(flatbuffer_protocol_test PRIVATE dnf_core)

add_test(NAME flatbuffer_protocol_test COMMAND flatbuffer_protocol_test)

add_executable(tcp_flatbuffer_schema_test tests/TcpFlatBufferSchemaTest.cpp)
target_link_libraries(tcp_flatbuffer_schema_test PRIVATE dnf_core)

add_test(NAME tcp_flatbuffer_schema_test COMMAND tcp_flatbuffer_schema_test)

add_executable(
    auth_flatbuffer_schema_test
    tests/AuthFlatBufferSchemaTest.cpp
)
target_link_libraries(auth_flatbuffer_schema_test PRIVATE dnf_core)

add_test(
    NAME auth_flatbuffer_schema_test
    COMMAND auth_flatbuffer_schema_test
)

add_executable(
    auth_flatbuffer_codec_test
    tests/AuthFlatBufferCodecTest.cpp
)
target_link_libraries(auth_flatbuffer_codec_test PRIVATE dnf_core)

add_test(
    NAME auth_flatbuffer_codec_test
    COMMAND auth_flatbuffer_codec_test
)

add_executable(
    auth_packet_dispatcher_test
    tests/AuthPacketDispatcherTest.cpp
)
target_link_libraries(auth_packet_dispatcher_test PRIVATE dnf_core)

add_test(
    NAME auth_packet_dispatcher_test
    COMMAND auth_packet_dispatcher_test
)

add_executable(
    auth_tls_session_test
    tests/AuthTlsSessionTest.cpp
    tests/TestTlsCertificate.cpp
)
target_link_libraries(auth_tls_session_test PRIVATE dnf_core)

add_test(
    NAME auth_tls_session_test
    COMMAND auth_tls_session_test
)

add_executable(
    auth_tls_server_test
    tests/AuthTlsServerTest.cpp
    tests/TestTlsCertificate.cpp
)
target_link_libraries(auth_tls_server_test PRIVATE dnf_core)

add_test(
    NAME auth_tls_server_test
    COMMAND auth_tls_server_test
)

add_executable(
    auth_server_application_test
    tests/AuthServerApplicationTest.cpp
    tests/TestTlsCertificate.cpp
)
target_link_libraries(auth_server_application_test PRIVATE dnf_core)

add_test(
    NAME auth_server_application_test
    COMMAND auth_server_application_test
)
set_tests_properties(
    auth_server_application_test
    PROPERTIES TIMEOUT 10
)

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

add_executable(
    dungeon_static_data_protocol_test
    tests/DungeonStaticDataProtocolTest.cpp
)
target_link_libraries(dungeon_static_data_protocol_test PRIVATE dnf_core)

add_test(
    NAME dungeon_static_data_protocol_test
    COMMAND dungeon_static_data_protocol_test
)

add_executable(dungeon_udp_session_test tests/DungeonUdpSessionTest.cpp)
target_link_libraries(dungeon_udp_session_test PRIVATE dnf_core)

add_test(NAME dungeon_udp_session_test COMMAND dungeon_udp_session_test)

add_executable(
    dungeon_combat_processor_test
    tests/DungeonCombatProcessorTest.cpp
)
target_link_libraries(dungeon_combat_processor_test PRIVATE dnf_core)

add_test(
    NAME dungeon_combat_processor_test
    COMMAND dungeon_combat_processor_test
)

add_executable(
    dungeon_movement_processor_test
    tests/DungeonMovementProcessorTest.cpp
)
target_link_libraries(dungeon_movement_processor_test PRIVATE dnf_core)

add_test(
    NAME dungeon_movement_processor_test
    COMMAND dungeon_movement_processor_test
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
