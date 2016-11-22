#
# Try to find SDL2 library and include path.
# Once done this will define
#
# SDL2_FOUND
# SDL2_INCLUDE_PATH
# SDL2_LIBRARY
# SDL2_MAIN_LIBRARY
# 

SET(SDL2_SEARCH_PATHS
	$ENV{SDL2_PATH}
	${DEPENDENCIES_ROOT}
	/usr			# APPLE
	/usr/local		# APPLE
	/opt/local		# APPLE
)

IF (MINGW)
FIND_PATH(SDL2_INCLUDE_PATH
    NAMES
       SDL.h
    PATHS
        ${SDL2_SEARCH_PATHS}
    PATH_SUFFIXES
        include
    DOC
        "The directory where SDL.h resides"
)

    FIND_LIBRARY( SDL2_LIBRARY
        NAMES SDL2
        PATHS
        ${SDL2_SEARCH_PATHS}
        PATH_SUFFIXES
        /lib/x86
        /lib
    )

    FIND_LIBRARY( SDL2_MAIN_LIBRARY
        NAMES SDL2Main
        PATHS
        ${SDL2_SEARCH_PATHS}
        PATH_SUFFIXES
        /lib/x86
        /lib
    )


ELSEIF (MSVC)

	FIND_PATH(SDL2_INCLUDE_PATH
    NAMES
       SDL.h
    PATHS
        ${SDL2_SEARCH_PATHS}
    PATH_SUFFIXES
        include
    DOC
        "The directory where SDL2.h resides"
	)
    FIND_LIBRARY( SDL2_LIBRARY
        NAMES SDL2
        PATHS
        ${SDL2_SEARCH_PATHS}
        PATH_SUFFIXES
        /lib/x86
        /lib
    )

    FIND_LIBRARY( SDL2_MAIN_LIBRARY
        NAMES SDL2main
        PATHS
        ${SDL2_SEARCH_PATHS}
        PATH_SUFFIXES
        /lib/x86
        /lib
    )
ENDIF ()


SET(SDL2_FOUND "NO")
IF (SDL2_INCLUDE_PATH AND SDL2_LIBRARY)
	SET(SDL2_LIBRARIES 
        ${SDL2_LIBRARY} 
        ${SDL2_MAIN_LIBRARY}
        )
	SET(SDL2_FOUND "YES")
    message("EXTERNAL LIBRARY 'SDL2' FOUND")
ELSE()
    message("ERROR: EXTERNAL LIBRARY 'SDL2' NOT FOUND")
ENDIF (SDL2_INCLUDE_PATH AND SDL2_LIBRARY)
