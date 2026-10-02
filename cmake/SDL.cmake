
if(NINTENDO_SWITCH)
	set(DEP_SDL_VER "69c1e47162c0951f952ca420cf5eb8d51cce88f1")
	download_dep_tarball(
		"SDL-switch"
		"${DEP_SDL_VER}"
		"https://github.com/rollerozxa/SDL/archive/${DEP_SDL_VER}.tar.gz"
		"SKIP"
	)
else()
	set(DEP_SDL_VER "3.4.14")
	download_dep_tarball(
		"SDL"
		"${DEP_SDL_VER}"
		"https://github.com/libsdl-org/SDL/releases/download/release-${DEP_SDL_VER}/SDL3-${DEP_SDL_VER}.tar.gz"
		"30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb"
	)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

set(DISABLED_FEATURES CAMERA GPU HAPTIC POWER RENDER SENSOR TESTS VULKAN)

if(SCREENSHOT_BUILD)
	list(APPEND DISABLED_FEATURES AUDIO DIALOG HIDAPI JOYSTICK OPENGLES WAYLAND)
endif()

foreach(feature ${DISABLED_FEATURES})
	set(SDL_${feature} OFF CACHE BOOL "" FORCE)
endforeach()

if(ANDROID OR HAIKU)
	enable_language(CXX)
endif()

if(HAIKU)
	add_definitions(-fPIC)
endif()

if(EMSCRIPTEN)
	set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
	set(SDL_EMSCRIPTEN_PERSISTENT_PATH "/storage" CACHE STRING "" FORCE)
endif()

add_definitions(-DSDL_LEAN_AND_MEAN=1)

if(NINTENDO_SWITCH)
	add_subdirectory(lib/SDL-switch EXCLUDE_FROM_ALL)
else()
	add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)
endif()

if(IOS)
	# The SDL build references Bluetooth APIs even when Principia does not
	# actively request Bluetooth access. App Store Connect requires the
	# corresponding purpose string to be present in the packaged Info.plist.
	function(principia_add_ios_bluetooth_usage_description)
		add_custom_command(TARGET principia POST_BUILD
			COMMAND /usr/libexec/PlistBuddy
				-c "Add :NSBluetoothAlwaysUsageDescription string Principia may use Bluetooth to communicate with compatible controllers and accessories."
				"$<TARGET_FILE_DIR:principia>/Info.plist"
			VERBATIM)
	endfunction()

	# SDL.cmake is included before the Principia target is created, so defer the
	# target-specific post-build command until the end of the top-level directory.
	cmake_language(DEFER CALL principia_add_ios_bluetooth_usage_description)
endif()
