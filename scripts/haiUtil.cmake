# 
# SRT - Secure, Reliable, Transport
# Copyright (c) 2017 Haivision Systems Inc.
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>
# 


# Useful for combinging paths

function(adddirname prefix lst out_lst)
        set(output)
        foreach(item ${lst})
                list(APPEND output "${prefix}/${item}")
        endforeach()
        set(${out_lst} ${${out_lst}} ${output} PARENT_SCOPE)
endfunction()

# Splits a version formed as "major.minor.patch" recorded in variable 'prefix'
# and writes it into variables started with 'prefix' and ended with _MAJOR, _MINOR and _PATCH.
MACRO(set_version_variables prefix value)
	string(REPLACE "." ";" VERSION_LIST ${value})
	list(GET VERSION_LIST 0 ${prefix}_MAJOR)
	list(GET VERSION_LIST 1 ${prefix}_MINOR)
	list(GET VERSION_LIST 2 ${prefix}_PATCH)
	set(${prefix}_DEFINESTR "")
ENDMACRO(set_version_variables)

# Sets given variable to 1, if the condition that follows it is satisfied.
# Otherwise set it to 0.
MACRO(set_if varname)
   IF(${ARGN})
     SET(${varname} 1)
   ELSE(${ARGN})
     SET(${varname} 0)
   ENDIF(${ARGN})
ENDMACRO(set_if)

FUNCTION(join_arguments outvar)
	set (output)

	foreach (i ${ARGN})
		set(output "${output} ${i}")
	endforeach()

	set (${outvar} ${output} PARENT_SCOPE)
ENDFUNCTION()

macro(srt_install_symlink filepath sympath)
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${filepath} ${sympath})")
    install(CODE "message(\"-- Created symlink: ${sympath} -> ${filepath}\")")
endmacro(srt_install_symlink)

MACRO(MafRead maffile)
	# ARGN contains the extra "section-variable" pairs
	# If empty, return nothing
	set (MAFREAD_TAGS
		SOURCES            # source files
		PUBLIC_HEADERS     # installable headers for include
		PROTECTED_HEADERS  # installable headers used by other headers
		PRIVATE_HEADERS    # non-installable headers
	)
	cmake_parse_arguments(MAFREAD_VAR "" "${MAFREAD_TAGS}" "" ${ARGN})
	# Arguments for these tags are variables to be filled
	# with the contents of particular section.
	# While reading the file, extract the section.
	# Section is recognized by either first uppercase character or space.

	# @c http://cmake.org/pipermail/cmake/2007-May/014222.html
	FILE(READ ${maffile} MAFREAD_CONTENTS)
	STRING(REGEX REPLACE ";" "\\\\;" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")
	STRING(REGEX REPLACE "\n" ";" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")

    #message("DEBUG: MAF FILE CONTENTS: ${MAFREAD_CONTENTS}")
    #message("DEBUG: PASSED VARIABLES:")
    #foreach(DEBUG_VAR ${MAFREAD_TAGS})
    #	message("DEBUG: ${DEBUG_VAR}=${MAFREAD_VAR_${DEBUG_VAR}}")
    #endforeach()

	# The unnamed section becomes SOURCES
	set (MAFREAD_VARIABLE ${MAFREAD_VAR_SOURCES})
	set (MAFREAD_UNASSIGNED "")

	FOREACH(MAFREAD_LINE ${MAFREAD_CONTENTS})
		# Test what this line is
		string(STRIP ${MAFREAD_LINE} MAFREAD_OLINE)
		string(SUBSTRING ${MAFREAD_OLINE} 0 1 MAFREAD_FIRST)
		#message("DEBUG: LINE='${MAFREAD_LINE}' FIRST='${MAFREAD_FIRST}'")

		# The 'continue' command is cmake 3.2 - very late discovery
		if (MAFREAD_FIRST STREQUAL "")
			#message("DEBUG: ... skipped: empty")
		elseif (MAFREAD_FIRST STREQUAL "#")
			#message("DEBUG: ... skipped: comment")
		else()
			# Will be skipped if the line was a comment/empty
			string(REGEX MATCH "[ A-Z]" MAFREAD_SECMARK ${MAFREAD_FIRST})
			if (MAFREAD_SECMARK STREQUAL "")
				# This isn't a section, it's a list element.
				#message("DEBUG: ITEM: ${MAFREAD_OLINE} --> ${MAFREAD_VARIABLE}")
				LIST(APPEND ${MAFREAD_VARIABLE} ${MAFREAD_OLINE})
			else()
				# It's a section - change the running variable
				# Make it section name
				STRING(REPLACE  " " "_" MAFREAD_SECNAME ${MAFREAD_OLINE})
				set(MAFREAD_VARIABLE ${MAFREAD_VAR_${MAFREAD_SECNAME}})
				if (MAFREAD_VARIABLE STREQUAL "")
					set(MAFREAD_VARIABLE MAFREAD_UNASSIGNED)
				endif()
				#message("DEBUG: NEW SECTION: '${MAFREAD_SECNAME}' --> VARIABLE: '${MAFREAD_VARIABLE}'")
			endif()
		endif()
	ENDFOREACH()
	
	# Final debug report
    #set (ALL_VARS "")
    #message("DEBUG: extracted variables:")
    #foreach(DEBUG_VAR ${MAFREAD_TAGS})
    #	list(APPEND ALL_VARS ${MAFREAD_VAR_${DEBUG_VAR}})
    #endforeach()
    #list(REMOVE_DUPLICATES ALL_VARS)
    #foreach(DEBUG_VAR ${ALL_VARS})
    #	message("DEBUG: --> ${DEBUG_VAR} = ${${DEBUG_VAR}}")
    #endforeach()
ENDMACRO(MafRead)

# New version of MafRead macro, which automatically adds directory
# prefix. This should also resolve each relative path.
MACRO(MafReadDir directory maffile)
	# ARGN contains the extra "section-variable" pairs
	# If empty, return nothing
	set (MAFREAD_TAGS
		SOURCES            # source files
		PUBLIC_HEADERS     # installable headers for include
		PROTECTED_HEADERS  # installable headers used by other headers
		PRIVATE_HEADERS    # non-installable headers
		OPTIONS
	)
	cmake_parse_arguments(MAFREAD_VAR "" "${MAFREAD_TAGS}" "" ${ARGN})
	# Arguments for these tags are variables to be filled
	# with the contents of particular section.
	# While reading the file, extract the section.
	# Section is recognized by either first uppercase character or space.

	# @c http://cmake.org/pipermail/cmake/2007-May/014222.html
	FILE(READ ${directory}/${maffile} MAFREAD_CONTENTS)
	STRING(REGEX REPLACE ";" "\\\\;" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")
	STRING(REGEX REPLACE "\n" ";" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")

    #message("DEBUG: MAF FILE CONTENTS: ${MAFREAD_CONTENTS}")
    #message("DEBUG: PASSED VARIABLES:")
    #foreach(DEBUG_VAR ${MAFREAD_TAGS})
    #	message("DEBUG: ${DEBUG_VAR}=${MAFREAD_VAR_${DEBUG_VAR}}")
    #endforeach()

	# The unnamed section becomes SOURCES
	set (MAFREAD_VARIABLE ${MAFREAD_VAR_SOURCES})
	set (MAFREAD_UNASSIGNED "")

	# Default section type. Another is 'flags'.
	set (MAFREAD_SECTION_TYPE file)

	FOREACH(MAFREAD_LINE ${MAFREAD_CONTENTS})
		# Test what this line is
		string(STRIP ${MAFREAD_LINE} MAFREAD_OLINE)
		string(SUBSTRING ${MAFREAD_OLINE} 0 1 MAFREAD_FIRST)
		#message("DEBUG: LINE='${MAFREAD_LINE}' FIRST='${MAFREAD_FIRST}'")

		# The 'continue' command is cmake 3.2 - very late discovery
		if (MAFREAD_FIRST STREQUAL "")
			#message("DEBUG: ... skipped: empty")
		elseif (MAFREAD_FIRST STREQUAL "#")
			#message("DEBUG: ... skipped: comment")
		else()
			# Will be skipped if the line was a comment/empty
			string(REGEX MATCH "[ A-Z-]" MAFREAD_SECMARK ${MAFREAD_FIRST})
			if (MAFREAD_SECMARK STREQUAL "")
				# This isn't a section, it's a list element.
				#message("DEBUG: ITEM: ${MAFREAD_OLINE} --> ${MAFREAD_VARIABLE}")
				if (${MAFREAD_SECTION_TYPE} STREQUAL file)
					get_filename_component(MAFREAD_OLINE ${directory}/${MAFREAD_OLINE} ABSOLUTE)
				endif()
				LIST(APPEND ${MAFREAD_VARIABLE} ${MAFREAD_OLINE})
			else()
				# It's a section - change the running variable
				# Make it section name
				STRING(REPLACE  " " "_" MAFREAD_SECNAME ${MAFREAD_OLINE})

				# The cmake's version of 'if (MAFREAD_SECNAME[0] == '-')' - sigh...
				string(SUBSTRING ${MAFREAD_SECNAME} 0 1 MAFREAD_SECNAME0)
				if (${MAFREAD_SECNAME0} STREQUAL "-")
					set (MAFREAD_SECTION_TYPE option)
					string(SUBSTRING ${MAFREAD_SECNAME} 1 -1 MAFREAD_SECNAME)
				else()
					set (MAFREAD_SECTION_TYPE file)
				endif()
				set(MAFREAD_VARIABLE ${MAFREAD_VAR_${MAFREAD_SECNAME}})
				if (MAFREAD_VARIABLE STREQUAL "")
					set(MAFREAD_VARIABLE MAFREAD_UNASSIGNED)
				endif()
				#message("DEBUG: NEW SECTION: '${MAFREAD_SECNAME}' --> VARIABLE: '${MAFREAD_VARIABLE}'")
			endif()
		endif()
	ENDFOREACH()
	
	# Final debug report
    #set (ALL_VARS "")
    #message("DEBUG: extracted variables:")
    #foreach(DEBUG_VAR ${MAFREAD_TAGS})
    #	list(APPEND ALL_VARS ${MAFREAD_VAR_${DEBUG_VAR}})
    #endforeach()
    #list(REMOVE_DUPLICATES ALL_VARS)
    #foreach(DEBUG_VAR ${ALL_VARS})
    #	message("DEBUG: --> ${DEBUG_VAR} = ${${DEBUG_VAR}}")
    #endforeach()
ENDMACRO(MafReadDir)

# NOTE: This is historical only. Not in use.
# It should be a similar interface to mafread.tcl like
# the above MafRead macro.
MACRO(GetMafHeaders directory outvar)
	EXECUTE_PROCESS(
		COMMAND ${CMAKE_MODULE_PATH}/mafread.tcl
			${CMAKE_SOURCE_DIR}/${directory}/HEADERS.maf
			"PUBLIC HEADERS"
			"PROTECTED HEADERS"
		OUTPUT_STRIP_TRAILING_WHITESPACE
		OUTPUT_VARIABLE ${outvar}
	)
	SEPARATE_ARGUMENTS(${outvar})
	adddirname(${CMAKE_SOURCE_DIR}/${directory} "${${outvar}}" ${outvar})
ENDMACRO(GetMafHeaders)
