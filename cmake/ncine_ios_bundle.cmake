# iOS application bundle
#
# An iOS app is a flat ".app" directory (no "Contents/MacOS" and "Contents/Resources" like on macOS): the
# executable, the Info.plist, the icons and every resource sit side by side at its root, and the whole thing is
# code-signed as one unit. CPack's Bundle generator only knows the macOS layout, and the macOS build's wrapper
# script (which `cd`s into Resources before starting the game) has no place on iOS, where the executable is
# started directly - so the bundle is assembled here, at build time, from what the executable target already
# is: CMake's MACOSX_BUNDLE property produces the iOS layout on its own when the target system is iOS.
#
# What is put into it:
#   - Info.plist   configured from Sources/Info-iOS.plist.in (bundle identifier, version, orientations,
#                  file sharing, launch screen - see the comments there)
#   - Content/     the game's own data, copied from NCINE_CONTENT_DIR (ContentResolver finds it next to the
#                  executable, see the iOS arm there)
#   - AppIcon*.png the home-screen icons, scaled from Sources/Icons/1024px.png with `sips` (part of macOS)
#
# Code signing: the Xcode generator signs on its own from the XCODE_ATTRIBUTE_* properties below (set
# NCINE_IOS_DEVELOPMENT_TEAM for a device build; without a team the build is left unsigned, which is what the
# simulator wants). With Ninja/Makefiles nothing signs the bundle, so a device build additionally needs
# NCINE_IOS_CODESIGN_IDENTITY (and the provisioning profile the identity belongs to, see
# NCINE_IOS_PROVISIONING_PROFILE) to run `codesign` after the build; the simulator runs unsigned bundles.

if(NOT IOS)
	return()
endif()

# Which of the two SDKs the build targets decides how the bundle declares itself (CFBundleSupportedPlatforms)
# and whether signing can be skipped
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
	set(NCINE_IOS_SIMULATOR TRUE)
	set(NCINE_IOS_PLATFORM_NAME "iPhoneSimulator")
else()
	set(NCINE_IOS_SIMULATOR FALSE)
	set(NCINE_IOS_PLATFORM_NAME "iPhoneOS")
endif()

if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
	# The Info.plist has to state a minimum version; the launch-screen dictionary used there is an iOS 14 key
	set(NCINE_IOS_MINIMUM_VERSION "14.0")
else()
	set(NCINE_IOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

set(NCINE_IOS_EXECUTABLE_NAME "${NCINE_APP}")
configure_file("${NCINE_SOURCE_DIR}/Info-iOS.plist.in" "${CMAKE_BINARY_DIR}/Info-iOS.plist" @ONLY)

set_target_properties(${NCINE_APP} PROPERTIES
	OUTPUT_NAME "${NCINE_IOS_EXECUTABLE_NAME}"
	MACOSX_BUNDLE TRUE
	MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info-iOS.plist"
	# Xcode generator: the same identity/team/bundle-id decisions as an Xcode project would make by hand
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${NCINE_IOS_BUNDLE_IDENTIFIER}"
	XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
	XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${NCINE_IOS_MINIMUM_VERSION}"
	XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
	XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
	XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
	XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks")

if(NCINE_IOS_DEVELOPMENT_TEAM)
	set_target_properties(${NCINE_APP} PROPERTIES
		XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${NCINE_IOS_DEVELOPMENT_TEAM}"
		XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
		XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer")
else()
	set_target_properties(${NCINE_APP} PROPERTIES
		XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
		XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
		XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()

# The bundle directory itself (the ".app"); for an iOS bundle TARGET_BUNDLE_CONTENT_DIR is the same directory
set(_iosBundleDir "$<TARGET_BUNDLE_CONTENT_DIR:${NCINE_APP}>")
# With Xcode the path above would contain ${EFFECTIVE_PLATFORM_NAME}, which the post-build scripts escape, so the files
# landed in a literal "Debug${EFFECTIVE_PLATFORM_NAME}" directory instead of the bundle; emit plain "Debug" paths instead
set_property(GLOBAL PROPERTY XCODE_EMIT_EFFECTIVE_PLATFORM_NAME OFF)

# Game content, next to the executable
if(IS_DIRECTORY "${NCINE_CONTENT_DIR}")
	add_custom_command(TARGET ${NCINE_APP} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${NCINE_CONTENT_DIR}" "${_iosBundleDir}/Content"
		COMMENT "Copying game content into the application bundle"
		VERBATIM)
endif()

# Home-screen icons (the sizes UIKit asks for on iPhone @2x/@3x and iPad @2x, named as CFBundleIconFiles
# expects them). Without an asset catalog these plain PNGs are the way to ship them, and `sips` is on every Mac.
if(EXISTS "${NCINE_SOURCE_DIR}/Icons/1024px.png")
	find_program(NCINE_SIPS_EXECUTABLE sips)
	if(NCINE_SIPS_EXECUTABLE)
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${NCINE_SIPS_EXECUTABLE} -z 120 120 "${NCINE_SOURCE_DIR}/Icons/1024px.png" --out "${_iosBundleDir}/AppIcon60x60@2x.png"
			COMMAND ${NCINE_SIPS_EXECUTABLE} -z 180 180 "${NCINE_SOURCE_DIR}/Icons/1024px.png" --out "${_iosBundleDir}/AppIcon60x60@3x.png"
			COMMAND ${NCINE_SIPS_EXECUTABLE} -z 152 152 "${NCINE_SOURCE_DIR}/Icons/1024px.png" --out "${_iosBundleDir}/AppIcon76x76@2x.png"
			COMMAND ${NCINE_SIPS_EXECUTABLE} -z 167 167 "${NCINE_SOURCE_DIR}/Icons/1024px.png" --out "${_iosBundleDir}/AppIcon83.5x83.5@2x.png"
			COMMENT "Scaling the application icons"
			VERBATIM)
	else()
		message(WARNING "sips not found, the application bundle will have no icons")
	endif()
endif()

# Signing for the single-configuration generators (Xcode does this itself). The simulator does not check
# signatures at all, a device refuses an unsigned bundle - and a bundle whose provisioning profile is missing
# or does not cover the bundle identifier just as much, which is what the profile copy below is for.
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
	if(NCINE_IOS_CODESIGN_IDENTITY)
		find_program(NCINE_CODESIGN_EXECUTABLE codesign)
		if(NOT NCINE_CODESIGN_EXECUTABLE)
			message(FATAL_ERROR "NCINE_IOS_CODESIGN_IDENTITY is set, but `codesign` was not found")
		endif()
		set(_signArgs "--force" "--sign" "${NCINE_IOS_CODESIGN_IDENTITY}" "--timestamp=none")
		if(NCINE_IOS_PROVISIONING_PROFILE)
			if(NOT EXISTS "${NCINE_IOS_PROVISIONING_PROFILE}")
				message(FATAL_ERROR "Provisioning profile not found at \"${NCINE_IOS_PROVISIONING_PROFILE}\"")
			endif()
			add_custom_command(TARGET ${NCINE_APP} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NCINE_IOS_PROVISIONING_PROFILE}" "${_iosBundleDir}/embedded.mobileprovision"
				VERBATIM)
		endif()
		if(NCINE_IOS_DEVELOPMENT_TEAM)
			# The entitlements a development-signed app carries: its own identifier and the team, plus
			# get-task-allow so a debugger (and the trace log over `devicectl`) can attach
			file(WRITE "${CMAKE_BINARY_DIR}/Entitlements-iOS.plist"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>application-identifier</key>
	<string>${NCINE_IOS_DEVELOPMENT_TEAM}.${NCINE_IOS_BUNDLE_IDENTIFIER}</string>
	<key>com.apple.developer.team-identifier</key>
	<string>${NCINE_IOS_DEVELOPMENT_TEAM}</string>
	<key>get-task-allow</key>
	<true/>
</dict>
</plist>
")
			list(APPEND _signArgs "--entitlements" "${CMAKE_BINARY_DIR}/Entitlements-iOS.plist")
		endif()
		# Signing has to be the LAST thing that touches the bundle: every file in it is sealed by the signature
		add_custom_command(TARGET ${NCINE_APP} POST_BUILD
			COMMAND ${NCINE_CODESIGN_EXECUTABLE} ${_signArgs} "$<TARGET_BUNDLE_DIR:${NCINE_APP}>"
			COMMENT "Signing the application bundle"
			VERBATIM)
	elseif(NOT NCINE_IOS_SIMULATOR)
		message(STATUS "The device bundle will not be signed (set NCINE_IOS_CODESIGN_IDENTITY, or use the Xcode generator with NCINE_IOS_DEVELOPMENT_TEAM)")
	endif()
endif()

message(STATUS "iOS bundle: ${NCINE_IOS_BUNDLE_IDENTIFIER} for ${NCINE_IOS_PLATFORM_NAME} ${NCINE_IOS_MINIMUM_VERSION}+ (${CMAKE_OSX_ARCHITECTURES})")
