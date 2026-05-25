cmake_minimum_required(VERSION 3.4.1)
project(Hin2n)
SET(CMAKE_VERBOSE_MAKEFILE ON)
# N2n information
set(N2N_VERSION 2.9.0)
set(N2N_OSNAME ${CMAKE_SYSTEM})
set(N2N_MODIFY_VERSION v2s_0.1.0)
set(N2N_MODIFY_AUTHOR "switchwang(https://github.com/switch-st) zhangbz(https://github.com/zhangbz)")
add_definitions(-DCMAKE_BUILD)

INCLUDE(TestBigEndian)
TEST_BIG_ENDIAN(UIP_CONF_BYTE_ORDER)
if(${UIP_CONF_BYTE_ORDER} EQUAL 1)
set(UIP_CONF_BYTE_ORDER UIP_BIG_ENDIAN)
else(${UIP_CONF_BYTE_ORDER} EQUAL 1)
set(UIP_CONF_BYTE_ORDER UIP_LITTLE_ENDIAN)
endif(${UIP_CONF_BYTE_ORDER} EQUAL 1)

# OpenSSL
SET(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/main/jniLibs/${ANDROID_ABI}" CACHE PATH "" FORCE)
SET(OPENSSL_LIBRARIES_DIR "${OPENSSL_ROOT_DIR}")
SET(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include" CACHE PATH "" FORCE)
SET(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/libssl.so" CACHE FILEPATH "" FORCE)
SET(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/libcrypto.so" CACHE FILEPATH "" FORCE)
SET(OPENSSL_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY}")
if(EXISTS "${OPENSSL_SSL_LIBRARY}")
set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}")
endif()
LINK_DIRECTORIES(${OPENSSL_LIBRARIES_DIR})
OPTION(N2N_OPTION_AES "USE AES" ON)
set(N2N_OPTION_AES ON)

if(CMAKE_BUILD_TYPE)
if(NOT ${CMAKE_BUILD_TYPE} STREQUAL Debug)
set(CMAKE_BUILD_TYPE Release)
endif(NOT ${CMAKE_BUILD_TYPE} STREQUAL Debug)
else(CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release)
endif(CMAKE_BUILD_TYPE)
add_definitions(-DCMAKE_BUILD)

set(CARES_STATIC ON CACHE BOOL "Build static c-ares library")
set(CARES_SHARED OFF CACHE BOOL "Do not build shared c-ares library")
set(CARES_INSTALL OFF CACHE BOOL "")
set(CARES_BUILD_TOOLS OFF CACHE BOOL "")
set(CARES_BUILD_TESTS OFF CACHE BOOL "")
add_subdirectory(src/main/cpp/c-ares)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(USE_CURL_HTTPS ON)
if(EXISTS "${OPENSSL_SSL_LIBRARY}")
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
else()
message(FATAL_ERROR "启用 curl HTTPS 需要 ${OPENSSL_SSL_LIBRARY}，但当前 ABI 缺少 libssl.so")
endif()
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SSPI ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ARES ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DEBUG ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MANUAL ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_VERBOSE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TLS_SRP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LIBCURL_OPTION ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_THREADED_RESOLVER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IPV6 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(ENABLE_BROTLI OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_HTTP2 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LIBIDN2 ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_PSL ON CACHE BOOL "" FORCE)
add_subdirectory(src/main/cpp/curl)

add_definitions(-DN2N_OSNAME=\"${N2N_OSNAME}\" -DN2N_MODIFY_VERSION=\"${N2N_MODIFY_VERSION}\" -DN2N_MODIFY_AUTHOR=\"${N2N_MODIFY_AUTHOR}\" -DUIP_CONF_BYTE_ORDER=\"${UIP_CONF_BYTE_ORDER}\")
add_definitions(-DGIT_RELEASE="" -DPACKAGE_VERSION="${N2N_VERSION}" -DPACKAGE_OSNAME="${CMAKE_SYSTEM}")
include_directories(src/main/cpp src/main/cpp/edge_jni src/main/cpp/slog src/main/cpp/tun2tap src/main/cpp/uip)

add_library(edge_jni SHARED src/main/cpp/edge_jni/edge_jni.c)
target_link_libraries(edge_jni edge_v3)
target_link_libraries(edge_jni edge_v2_ipv6)
target_link_libraries(edge_jni edge_v2s)
target_link_libraries(edge_jni edge_v2)
target_link_libraries(edge_jni edge_v1)
target_link_libraries(edge_jni slog)
target_link_libraries(edge_jni ${OPENSSL_LIBRARIES})

add_library(edge_v2s SHARED src/main/cpp/n2n_v2s/edge.c)
target_include_directories(edge_v2s PUBLIC
	    ${CMAKE_CURRENT_BINARY_DIR}/c-ares
            src/main/cpp/c-ares
            src/main/cpp/c-ares/include
            ${CMAKE_CURRENT_BINARY_DIR}/curl
            src/main/cpp/curl
            src/main/cpp/curl/include
            src/main/cpp/n2n_v2s
        )
target_link_libraries(edge_v2s n2n_v2s)
target_link_libraries(edge_v2s slog c-ares libcurl)

# -- ntop n2n
add_library(edge_v2 SHARED
            src/main/cpp/n2n_v2/src/n2n.c
            src/main/cpp/n2n_v2/src/edge_utils.c
            src/main/cpp/n2n_v2/src/wire.c
            src/main/cpp/n2n_v2/src/minilzo.c
            src/main/cpp/n2n_v2/src/twofish.c
            src/main/cpp/n2n_v2/src/speck.c
            src/main/cpp/n2n_v2/src/pearson.c
            src/main/cpp/n2n_v2/src/header_encryption.c
            src/main/cpp/n2n_v2/src/transform_null.c
            src/main/cpp/n2n_v2/src/transform_tf.c
            src/main/cpp/n2n_v2/src/transform_aes.c
            src/main/cpp/n2n_v2/src/transform_speck.c
            src/main/cpp/n2n_v2/src/transform_cc20.c
            src/main/cpp/n2n_v2/src/random_numbers.c

            src/main/cpp/edge_jni/tuntap_android_v2.c
            src/main/cpp/edge_jni/edge_android_v2.c
        )
target_compile_definitions(edge_v2 PUBLIC N2N_HAVE_AES HAVE_OPENSSL_1_1)
target_include_directories(edge_v2 PUBLIC
	    ${CMAKE_CURRENT_BINARY_DIR}/c-ares
            src/main/cpp/c-ares
            src/main/cpp/c-ares/include
            ${CMAKE_CURRENT_BINARY_DIR}/curl
            src/main/cpp/curl
            src/main/cpp/curl/include
            src/main/cpp/n2n_v2/include
            ${OPENSSL_INCLUDE_DIR}
        )
target_link_libraries(edge_v2 uip c-ares libcurl)
target_link_libraries(edge_v2 ${OPENSSL_LIBRARIES})

add_library(edge_v3 SHARED
        src/main/cpp/n2n_v3/src/n2n.c
        src/main/cpp/n2n_v3/src/edge_management.c
        src/main/cpp/n2n_v3/src/curve25519.c
        src/main/cpp/n2n_v3/src/auth.c
        src/main/cpp/n2n_v3/src/edge_utils.c
        src/main/cpp/n2n_v3/src/wire.c
        src/main/cpp/n2n_v3/src/minilzo.c
        src/main/cpp/n2n_v3/src/speck.c
        src/main/cpp/n2n_v3/src/pearson.c
        src/main/cpp/n2n_v3/src/tf.c
        src/main/cpp/n2n_v3/src/header_encryption.c
        src/main/cpp/n2n_v3/src/transform_null.c
        src/main/cpp/n2n_v3/src/transform_tf.c
        src/main/cpp/n2n_v3/src/transform_aes.c
        src/main/cpp/n2n_v3/src/transform_speck.c
        src/main/cpp/n2n_v3/src/transform_cc20.c
        src/main/cpp/n2n_v3/src/cc20.c
        src/main/cpp/n2n_v3/src/aes.c
        src/main/cpp/n2n_v3/src/network_traffic_filter.c
        src/main/cpp/n2n_v3/src/sn_selection.c
        src/main/cpp/n2n_v3/src/random_numbers.c
        src/main/cpp/edge_jni/tuntap_android_v2.c
        src/main/cpp/edge_jni/edge_android_v2.c
        )
target_compile_definitions(edge_v3 PUBLIC N2N_HAVE_AES HAVE_OPENSSL_1_1)
target_compile_options(edge_v3 PRIVATE -DN2N_V3)
target_include_directories(edge_v3 PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/c-ares	
        src/main/cpp/c-ares
        src/main/cpp/c-ares/include
        ${CMAKE_CURRENT_BINARY_DIR}/curl
        src/main/cpp/curl
        src/main/cpp/curl/include
        src/main/cpp/n2n_v3/include
        ${OPENSSL_INCLUDE_DIR}
        )
target_link_libraries(edge_v3 uip c-ares libcurl)
target_link_libraries(edge_v3 ${OPENSSL_LIBRARIES})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/n2n_v2_ipv6/CMakeLists.txt" N2N_V2_IPV6_VERSION_LINE REGEX "^set\\(N2N_VERSION[ \t]+.*\\)")
string(REGEX REPLACE "^set\\(N2N_VERSION[ \t]+\"?([^\"\\) ]+)\"?\\).*" "\\1" N2N_V2_IPV6_VERSION "${N2N_V2_IPV6_VERSION_LINE}")
if(NOT N2N_V2_IPV6_VERSION)
message(FATAL_ERROR "Failed to read v2-ipv6 N2N_VERSION from src/main/cpp/n2n_v2_ipv6/CMakeLists.txt")
endif()

add_library(edge_v2_ipv6 SHARED
        src/main/cpp/n2n_v2_ipv6/n2n.c
        src/main/cpp/n2n_v2_ipv6/wire.c
        src/main/cpp/n2n_v2_ipv6/version.c
        src/main/cpp/n2n_v2_ipv6/aes.c
        src/main/cpp/n2n_v2_ipv6/sha.c
        src/main/cpp/n2n_v2_ipv6/twofish.c
        src/main/cpp/n2n_v2_ipv6/speck.c
        src/main/cpp/n2n_v2_ipv6/pearson.c
        src/main/cpp/n2n_v2_ipv6/cc20.c
        src/main/cpp/n2n_v2_ipv6/transform_null.c
        src/main/cpp/n2n_v2_ipv6/transform_tf.c
        src/main/cpp/n2n_v2_ipv6/transform_aes.c
        src/main/cpp/n2n_v2_ipv6/transform_cc20.c
        src/main/cpp/n2n_v2_ipv6/transform_speck.c
        src/main/cpp/n2n_v2_ipv6/n2n_keyfile.c
        src/main/cpp/n2n_v2_ipv6/minilzo.c
        src/main/cpp/n2n_v2_ipv6/upnp.c
        src/main/cpp/edge_jni/edge_v2_ipv6_embedded.c
        src/main/cpp/edge_jni/tuntap_android_v2_ipv6.c
        src/main/cpp/edge_jni/edge_android_v2_ipv6.c
        src/main/cpp/edge_jni/ifaddrs_android_v2_ipv6.c
        )
target_compile_definitions(edge_v2_ipv6 PUBLIC N2N_HAVE_AES N2N_HAVE_CC20 __ANDROID_NDK__)
target_compile_definitions(edge_v2_ipv6 PRIVATE N2N_VERSION=\"${N2N_V2_IPV6_VERSION}\")
set_target_properties(edge_v2_ipv6 PROPERTIES LINK_FLAGS "-Wl,--wrap=socket -Wl,--wrap=exit -Wl,--wrap=abort -Wl,--wrap=setuid -Wl,--wrap=setgid -Wl,--wrap=setreuid -Wl,--wrap=setregid")
target_include_directories(edge_v2_ipv6 PUBLIC
        src/main/cpp/n2n_v2_ipv6
        src/main/cpp/tun2tap
        src/main/cpp/uip
        )
target_link_libraries(edge_v2_ipv6 uip log)

add_library(edge_v1 SHARED
                src/main/cpp/n2n_v1/edge.c
            )
target_include_directories(edge_v1 PUBLIC
	    ${CMAKE_CURRENT_BINARY_DIR}/c-ares
            src/main/cpp/c-ares
            src/main/cpp/c-ares/include
            ${CMAKE_CURRENT_BINARY_DIR}/curl
            src/main/cpp/curl
            src/main/cpp/curl/include
            src/main/cpp/n2n_v1
        )
target_link_libraries(edge_v1 n2n_v1)
target_link_libraries(edge_v1 slog c-ares libcurl)

add_library(n2n_v2s SHARED
                src/main/cpp/n2n_v2s/n2n.c
                src/main/cpp/n2n_v2s/n2n_keyfile.c
                src/main/cpp/n2n_v2s/wire.c
                src/main/cpp/n2n_v2s/minilzo.c
                src/main/cpp/n2n_v2s/twofish.c
                src/main/cpp/n2n_v2s/transform_null.c
                src/main/cpp/n2n_v2s/transform_tf.c
                src/main/cpp/n2n_v2s/transform_aes.c
                src/main/cpp/n2n_v2s/android/tuntap_android.c
                src/main/cpp/n2n_v2s/version.c
            )
target_compile_definitions(n2n_v2s PRIVATE N2N_VERSION=\"${N2N_VERSION}\")
target_link_libraries(n2n_v2s uip)
target_link_libraries(n2n_v2s slog)

add_library(n2n_v1 SHARED
                src/main/cpp/n2n_v1/n2n.c
                src/main/cpp/n2n_v1/minilzo.c
                src/main/cpp/n2n_v1/twofish.c
                src/main/cpp/n2n_v1/android/tuntap_android.c
                src/main/cpp/n2n_v1/version.c
            )
target_compile_definitions(n2n_v1 PRIVATE N2N_VERSION=\"${N2N_VERSION}\")
target_link_libraries(n2n_v1 uip)
target_link_libraries(n2n_v1 slog)


add_library(uip SHARED
                src/main/cpp/uip/uip.c
                src/main/cpp/uip/uip_arp.c
                src/main/cpp/tun2tap/tun2tap.c
            )

add_library(slog SHARED src/main/cpp/slog/slog.c)
target_link_libraries(slog log)
