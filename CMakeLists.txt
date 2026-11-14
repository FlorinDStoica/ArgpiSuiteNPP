cmake_minimum_required(VERSION 3.20)
project(ArgpiSuiteFormatter VERSION 5.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /WX /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

add_library(argpiformat STATIC
    src/format/Format.cpp
    src/format/FormatDoc.cpp
    src/format/FormatDocPrinter.cpp
    src/format/FormatLanguage.cpp
    src/format/FormatLineEnding.cpp
    src/format/FormatOptions.cpp
    src/format/FormatUnicode.cpp
    src/format/brace/FormatBraceFormatter.cpp
    src/format/json/FormatJsonEquivalence.cpp
    src/format/json/FormatJsonParser.cpp
    src/format/json/FormatJsonPrinter.cpp
    src/format/markdown/FormatMarkdownFormatter.cpp
    src/format/xml/FormatMarkupFormatter.cpp
    src/format/yaml/FormatYamlFormatter.cpp
)

target_include_directories(argpiformat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/format
    ${CMAKE_CURRENT_SOURCE_DIR}/src/format/json
)

add_executable(argpiformat_tests
    tests/test_main.cpp
    tests/test_docprinter.cpp
    tests/test_fixtures.cpp
    tests/test_json.cpp
    tests/test_languages.cpp
    tests/test_options.cpp
    tests/test_unicode.cpp
)
target_link_libraries(argpiformat_tests PRIVATE argpiformat)
target_compile_definitions(argpiformat_tests PRIVATE
    ARGPI_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)

add_executable(argpiformat_bench bench/Bench.cpp)
target_link_libraries(argpiformat_bench PRIVATE argpiformat)

add_executable(argpifmt_cli tools/fmt_cli.cpp)
target_link_libraries(argpifmt_cli PRIVATE argpiformat)

enable_testing()
add_test(NAME formatter_tests COMMAND argpiformat_tests)

if (WIN32)
    add_executable(argpimenu_tests
        tests/test_main.cpp
        tests/test_menu.cpp
        src/plugin/MenuBuilder.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpimenu_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpimenu_tests PRIVATE UNICODE _UNICODE)
    add_test(NAME menu_tests COMMAND argpimenu_tests)

    add_executable(argpipassword_tests
        tests/test_main.cpp
        tests/test_password.cpp
        src/password/CryptoHash.cpp
        src/password/PasswordService.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpipassword_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpipassword_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(argpipassword_tests PRIVATE bcrypt)
    add_test(NAME password_tests COMMAND argpipassword_tests)

    add_executable(argpicrypto_tests
        tests/test_main.cpp
        tests/test_crypto.cpp
        src/crypto/CryptoService.cpp
        src/crypto/EncodingService.cpp
        src/password/CryptoHash.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpicrypto_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpicrypto_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(argpicrypto_tests PRIVATE bcrypt)
    add_test(NAME crypto_tests COMMAND argpicrypto_tests)

    add_executable(argpiencoding_tests
        tests/test_main.cpp
        tests/test_encoding.cpp
        src/crypto/EncodingService.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpiencoding_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpiencoding_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    add_test(NAME encoding_tests COMMAND argpiencoding_tests)

    add_executable(argpigenerator_tests
        tests/test_main.cpp
        tests/test_generator.cpp
        src/generator/IdGeneratorService.cpp
        src/password/CryptoHash.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpigenerator_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpigenerator_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(argpigenerator_tests PRIVATE bcrypt)
    add_test(NAME generator_tests COMMAND argpigenerator_tests)

    add_executable(argpihash_tests
        tests/test_main.cpp
        tests/test_hash.cpp
        src/hash/HashService.cpp
        src/crypto/EncodingService.cpp
        src/password/CryptoHash.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpihash_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpihash_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(argpihash_tests PRIVATE bcrypt)
    add_test(NAME hash_tests COMMAND argpihash_tests)

    add_executable(argpitools_tests
        tests/test_main.cpp
        tests/test_tools.cpp
        src/tools/JwtDecoderService.cpp
        src/tools/PathQueryService.cpp
        src/tools/CsvToolService.cpp
        src/tools/DedupeSortService.cpp
        src/hash/HashService.cpp
        src/crypto/EncodingService.cpp
        src/password/CryptoHash.cpp
        src/plugin/TextUtil.cpp
    )
    target_include_directories(argpitools_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(argpitools_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(argpitools_tests PRIVATE argpiformat bcrypt)
    add_test(NAME tools_tests COMMAND argpitools_tests)
endif()
