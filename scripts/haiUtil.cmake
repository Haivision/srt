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
        set(${out_lst} ${output} PARENT_SCOPE)
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

