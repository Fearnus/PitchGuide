# ONNX Runtime from Microsoft's NuGet package (Microsoft.ML.OnnxRuntime).
#
# The build used to download the release archives from
# github.com/microsoft/onnxruntime, which no longer exists. The NuGet package
# carries the same official builds for every platform; this unpacks it into
# the layout of those archives (include/ and lib/), which the rest of the
# build and packaging/linux/build_installer.sh expect.
#
# PLATFORM is one of win-x64, linux-x64, osx-arm64, osx-x64, osx-universal2.
function(pitchnet_unpack_onnxruntime_nuget PACKAGE_DIR OUT_DIR PLATFORM VERSION)
    set(native "${PACKAGE_DIR}/runtimes")
    set(lib "${OUT_DIR}/lib")
    file(REMOVE_RECURSE "${OUT_DIR}")
    file(MAKE_DIRECTORY "${lib}")

    if(PLATFORM STREQUAL "win-x64")
        file(GLOB files "${native}/win-x64/native/*")
        file(COPY ${files} DESTINATION "${lib}")
    elseif(PLATFORM STREQUAL "linux-x64")
        # The library's SONAME is libonnxruntime.so.1: name it as the archive did.
        set(so "libonnxruntime.so.${VERSION}")
        file(COPY_FILE "${native}/linux-x64/native/libonnxruntime.so" "${lib}/${so}")
        file(COPY_FILE "${native}/linux-x64/native/libonnxruntime_providers_shared.so"
             "${lib}/libonnxruntime_providers_shared.so")
        file(CREATE_LINK "${so}" "${lib}/libonnxruntime.so.1" SYMBOLIC)
        file(CREATE_LINK "libonnxruntime.so.1" "${lib}/libonnxruntime.so" SYMBOLIC)
    elseif(PLATFORM MATCHES "^osx-")
        # The install name is @rpath/libonnxruntime.<version>.dylib, as in the archive.
        set(dylib "libonnxruntime.${VERSION}.dylib")
        if(PLATFORM STREQUAL "osx-universal2")
            execute_process(
                COMMAND lipo -create
                        "${native}/osx-arm64/native/libonnxruntime.dylib"
                        "${native}/osx-x64/native/libonnxruntime.dylib"
                        -output "${lib}/${dylib}"
                RESULT_VARIABLE result)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "lipo could not merge the ONNX Runtime dylibs (${result})")
            endif()
        else()
            file(COPY_FILE "${native}/${PLATFORM}/native/libonnxruntime.dylib" "${lib}/${dylib}")
        endif()
        file(CREATE_LINK "${dylib}" "${lib}/libonnxruntime.dylib" SYMBOLIC)
    else()
        message(FATAL_ERROR "No ONNX Runtime NuGet layout for ${PLATFORM}")
    endif()

    # Headers last: the build takes their presence to mean the copy is complete.
    file(COPY "${PACKAGE_DIR}/build/native/include" DESTINATION "${OUT_DIR}")
endfunction()
