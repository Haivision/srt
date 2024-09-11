# original file from obs-studio:
#  https://github.com/obsproject/obs-studio
#  /cmake/Modules/FindMbedTLS.cmake
#
# Once done these will be defined:
#
#  LIBMBEDTLS_FOUND
#  LIBMBEDTLS_INCLUDE_DIRS
#  LIBMBEDTLS_LIBRARIES
#
# For use in OBS:
#
#  MBEDTLS_INCLUDE_DIR

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_MBEDTLS QUIET mbedtls)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

# If we're on MacOS or Linux, please try to statically-link mbedtls.
if(STATIC_MBEDTLS AND (APPLE OR UNIX))
	set(_MBEDTLS_LIBRARIES libmbedtls.a)
	set(_MBEDCRYPTO_LIBRARIES libmbedcrypto.a)
	set(_MBEDX509_LIBRARIES libmbedx509.a)
endif()

find_path(MBEDTLS_INCLUDE_DIR
	NAMES mbedtls/ssl.h
	HINTS
		${MBEDTLS_PREFIX}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include
	PATH_SUFFIXES
		include)

find_library(MBEDTLS_LIB
	NAMES ${_MBEDTLS_LIBRARIES} mbedtls libmbedtls
	HINTS
		${MBEDTLS_PREFIX}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

find_library(MBEDCRYPTO_LIB
	NAMES ${_MBEDCRYPTO_LIBRARIES} mbedcrypto libmbedcrypto
	HINTS
		${MBEDTLS_PREFIX}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

find_library(MBEDX509_LIB
	NAMES ${_MBEDX509_LIBRARIES} mbedx509 libmbedx509
	HINTS
		${MBEDTLS_PREFIX}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

# Sometimes mbedtls is split between three libs, and sometimes it isn't.
# If it isn't, let's check if the symbols we need are all in MBEDTLS_LIB.
if(MBEDTLS_LIB AND NOT MBEDCRYPTO_LIB AND NOT MBEDX509_LIB)
	set(CMAKE_REQUIRED_LIBRARIES ${MBEDTLS_LIB})
	set(CMAKE_REQUIRED_INCLUDES ${MBEDTLS_INCLUDE_DIR})
	check_symbol_exists(mbedtls_x509_crt_init "mbedtls/x509_crt.h" MBEDTLS_INCLUDES_X509)
	check_symbol_exists(mbedtls_sha256_init "mbedtls/sha256.h" MBEDTLS_INCLUDES_CRYPTO)
	unset(CMAKE_REQUIRED_INCLUDES)
	unset(CMAKE_REQUIRED_LIBRARIES)
endif()

# If we find all three libraries, then go ahead.
if(MBEDTLS_LIB AND MBEDCRYPTO_LIB AND MBEDX509_LIB)
	set(LIBMBEDTLS_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
	set(LIBMBEDTLS_LIBRARIES ${MBEDTLS_LIB} ${MBEDCRYPTO_LIB} ${MBEDX509_LIB})
	set(MBEDTLS_INCLUDE_DIRS ${LIBMBEDTLS_INCLUDE_DIRS})
	set(MBEDTLS_LIBRARIES ${LIBMBEDTLS_LIBRARIES})

# Otherwise, if we find MBEDTLS_LIB, and it has both CRYPTO and x509
# within the single lib (i.e. a windows build environment), then also
# feel free to go ahead.
elseif(MBEDTLS_LIB AND MBEDTLS_INCLUDES_CRYPTO AND MBEDTLS_INCLUDES_X509)
	set(LIBMBEDTLS_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
	set(LIBMBEDTLS_LIBRARIES ${MBEDTLS_LIB})
	set(MBEDTLS_INCLUDE_DIRS ${LIBMBEDTLS_INCLUDE_DIRS})
	set(MBEDTLS_LIBRARIES ${LIBMBEDTLS_LIBRARIES})
endif()

# Now we've accounted for the 3-vs-1 library case:
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS DEFAULT_MSG MBEDTLS_LIBRARIES MBEDTLS_INCLUDE_DIRS)
mark_as_advanced(MBEDTLS_INCLUDE_DIR MBEDTLS_LIBRARIES MBEDTLS_INCLUDE_DIRS)
